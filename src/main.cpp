// Карманный голосовой ассистент: запись → Whisper → DeepSeek → экран.
//
// Управление:
//   BtnA (зажать) — запись, отпустить — отправить
//   BtnA с экрана ответа — новый вопрос
//   BtnB (тап)    — следующая страница, на последней — очистка
//   BtnB (удержать) — очистка экрана и памяти диалога
//
// Состояния: Idle → Recording → Sending → Recognizing → Streaming → Showing.
// Всё рисование — в ui.cpp; здесь только диспетчер и сеть.

#include <Arduino.h>
#include <M5Unified.h>

#include <string>
#include <vector>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "stt.h"
#include "llm.h"
#include "ui.h"
#include "font_jbmono.h"
#include "pure/wav.h"
#include "pure/text_wrap.h"
#include "pure/chat_history.h"

namespace {

constexpr int kWhite  = lgfx::colors::TFT_WHITE;
constexpr int kRed    = lgfx::colors::TFT_RED;

constexpr int kBodyW = kScreenW - 2 * ui::kMargin;

uint8_t* g_audio = nullptr;   // PSRAM: заголовок + аудио
int16_t  g_chunk[kDmaChunkBytes / 2];

enum class State { Idle, Recording, Sending, Recognizing, Streaming, Showing };
State g_state = State::Idle;

std::vector<std::string> g_lines;
size_t g_page = 0;

chat::History g_history(kChatMaxTurns);

// Волны: амплитуды последних кусков записи, свежие справа.
uint8_t g_wave[ui::kWaveBars] = {0};
size_t  g_waveN = 0;

uint32_t g_phaseStart = 0;   // начало текущей фазы
uint32_t g_lastDraw = 0;     // троттлинг перерисовок
String   g_stream;           // накопленный текст ответа
size_t   g_streamTarget = kStreamTargetChars;

// Ширина любого символа в моноширинном шрифте — константа.
int measureMono(const char*, size_t, void*) { return kFont_jbmono_XAdvance; }

void wrapInto(const char* text, std::vector<std::string>& out) {
  textwrap::wrap(text, kBodyW, measureMono, nullptr, out);
}

void showError(const char* text) {
  g_state = State::Showing;
  wrapInto(text, g_lines);
  g_page = 0;
  ui::text(g_lines, 0, kRed);
}

void showIdle() {
  g_state = State::Idle;
  ui::idle(g_history.turnCount());
}

void pushWave(uint8_t amp) {
  if (g_waveN < ui::kWaveBars) {
    g_wave[g_waveN++] = amp;
    return;
  }
  memmove(g_wave, g_wave + 1, ui::kWaveBars - 1);
  g_wave[ui::kWaveBars - 1] = amp;
}

// Пиковая амплитуда куска -> 0..255. Речь идёт на уровне ~−18 dBFS, поэтому
// делим с запасом усиления, чтобы столбики были заметными.
uint8_t peakOf(const int16_t* s, size_t n) {
  int32_t peak = 0;
  for (size_t i = 0; i < n; ++i) {
    const int32_t v = s[i] < 0 ? -s[i] : s[i];
    if (v > peak) peak = v;
  }
  int32_t a = peak / 32;
  if (a > 255) a = 255;
  return (uint8_t)a;
}

// --- обратные вызовы индикатора ---

void onUpload(size_t sent, size_t total, void*) {
  const uint32_t now = millis();
  if (sent < total && now - g_lastDraw < 80) return;
  g_lastDraw = now;
  const int pct = total ? (int)(40ULL * sent / total) : 0;
  ui::progress("Отправка", pct, now);
}

// Прокси молчит, пока Groq распознаёт: анимация по времени.
void onSttTick(void*) {
  const uint32_t now = millis();
  if (now - g_lastDraw < 100) return;
  g_lastDraw = now;
  int pct = 42 + (int)((now - g_phaseStart) / 60);
  if (pct > 54) pct = 54;
  ui::progress("Распознаю", pct, now);
}

// Кусок ответа пришёл: копим текст и обновляем экран.
void onDelta(const char* piece, size_t len, size_t activity, void*) {
  if (len > 0) g_stream.concat(piece);

  const uint32_t now = millis();
  if (now - g_lastDraw < 90 && activity < g_streamTarget) return;
  g_lastDraw = now;

  const size_t a = activity < g_streamTarget ? activity : g_streamTarget;
  int pct = 58 + (int)(42ULL * a / g_streamTarget);
  if (pct > 99) pct = 99;

  if (len > 0) {
    wrapInto(g_stream.c_str(), g_lines);
    const size_t from =
        g_lines.size() > ui::kBodyLines ? g_lines.size() - ui::kBodyLines : 0;
    ui::text(g_lines, from, kWhite);
  }
  ui::bottomBar(pct, now);
}

// Захват audioBytes байт в PSRAM. Возвращает число записанных байт.
size_t record(size_t audioBytes) {
  if (!M5.Mic.isRunning()) {
    if (!M5.Mic.begin()) {
      Serial.println("[main] микрофон не стартовал");
      return 0;
    }
  }

  const size_t totalSamples = audioBytes / 2;
  const size_t chunkSamples = sizeof(g_chunk) / 2;

  // Прогрев: первые 200 мс — переходный процесс PDM, не звук.
  {
    const size_t warm = (size_t)kSampleRate * 200 / 1000;
    size_t got = 0;
    while (got < warm) {
      const size_t want = min(warm - got, chunkSamples);
      if (!M5.Mic.record(g_chunk, want, kSampleRate)) { delay(1); continue; }
      while (M5.Mic.isRecording()) delay(1);
      got += want;
    }
  }

  size_t done = 0;
  const uint32_t started = millis();
  while (done < totalSamples && (millis() - started) < (kMaxRecordMs + 2000)) {
    const size_t want = min(totalSamples - done, chunkSamples);
    if (!M5.Mic.record(g_chunk, want, kSampleRate)) { delay(1); continue; }
    while (M5.Mic.isRecording()) delay(1);
    memcpy(g_audio + wav::headerSize() + done * 2, g_chunk, want * 2);
    done += want;

    pushWave(peakOf(g_chunk, want));

    M5.update();
    const uint32_t elapsed = millis() - started;
    if (millis() - g_lastDraw > 90) {
      g_lastDraw = millis();
      const uint32_t remain =
          elapsed >= kMaxRecordMs ? 0 : kMaxRecordMs - elapsed;
      ui::recording(remain, g_wave, g_waveN);
    }
    if (!M5.BtnA.isPressed()) break;
  }

  M5.Mic.end();
  return done * 2;
}

// Полный цикл: запись → STT → LLM → вывод.
void runQuestion() {
  if (WiFi.status() != WL_CONNECTED) {
    ui::progress("Wi-Fi...", 0, millis());
    if (!net::connectWifi(kWifiTimeoutMs)) {
      showError(net::lastError());
      delay(2000);
      showIdle();
      return;
    }
  }

  g_state = State::Recording;
  g_waveN = 0;
  g_lastDraw = 0;
  ui::recording(kMaxRecordMs, g_wave, g_waveN);
  M5.Speaker.tone(2000, 100);

  const size_t got = record(kAudioBytes);
  const uint32_t ms = (uint32_t)(got / 2) * 1000 / kSampleRate;
  Serial.printf("[main] записано %u байт (%u мс)\n", (unsigned)got, ms);

  if (got < (size_t)kSampleRate * 2 * kMinRecordMs / 1000) {
    ui::progress("Слишком коротко", 0, millis());
    delay(1200);
    showIdle();
    return;
  }

  // Прокси на VPS принимает сырой LPCM без WAV-заголовка.
  const uint8_t* pcm = g_audio + wav::headerSize();

  g_state = State::Sending;
  g_phaseStart = millis();
  g_lastDraw = 0;
  ui::progress("Отправка", 0, millis());

  stt::Hooks hooks;
  hooks.onUpload = onUpload;
  hooks.onTick = onSttTick;

  g_state = State::Recognizing;
  stt::Result s = stt::transcribe(pcm, got, hooks);
  if (!s.ok) {
    Serial.printf("[main] ошибка STT: %s\n", s.error.c_str());
    showError(s.error.c_str());
    return;
  }
  Serial.printf("[main] текст: %s\n", s.text.c_str());

  // Вопрос распознан — спрашиваем модель потоком.
  g_state = State::Streaming;
  g_stream = "";
  g_lastDraw = 0;
  ui::progress("Отвечаю", 58, millis());

  llm::Stream st;
  st.onDelta = onDelta;
  llm::Reply a = llm::ask(g_history.messages(), s.text, st);

  if (a.ok) {
    Serial.printf("[main] ответ: %s\n", a.text.c_str());
    g_history.addUser(s.text.c_str());
    g_history.addAssistant(a.text.c_str());
    g_history.trim(kChatMaxChars);
    Serial.printf("[main] в памяти %u обменов\n",
                  (unsigned)g_history.turnCount());

    g_state = State::Showing;
    g_page = 0;
    wrapInto(a.text.c_str(), g_lines);
    ui::text(g_lines, 0, kWhite);
    ui::pageIndicator(0, textwrap::pageCount(g_lines.size(), ui::kBodyLines));
  } else {
    Serial.printf("[main] ошибка LLM: %s\n", a.error.c_str());
    showError(a.error.c_str());
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(kScreenRotation);
  ui::begin();

  // Порог «долгого» нажатия BtnB — по спеке.
  M5.BtnB.setHoldThresh(kLongPressMs);

  const size_t total = wav::totalSize(kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    M5.Display.fillScreen(lgfx::colors::TFT_BLACK);
    M5.Display.setTextColor(lgfx::colors::TFT_RED, lgfx::colors::TFT_BLACK);
    M5.Display.setCursor(ui::kMargin, 30);
    M5.Display.print("Нет PSRAM");
    Serial.println("[main] ОШИБКА: PSRAM недоступна");
    while (true) delay(1000);
  }

  // Микрофон включается только на время записи; здесь проверяем, что он есть.
  if (!M5.Mic.begin()) {
    M5.Display.fillScreen(lgfx::colors::TFT_BLACK);
    M5.Display.setTextColor(lgfx::colors::TFT_RED, lgfx::colors::TFT_BLACK);
    M5.Display.setCursor(ui::kMargin, 30);
    M5.Display.print("Микрофон не стартовал");
    while (true) delay(1000);
  }
  M5.Mic.end();

  ui::progress("Wi-Fi...", 0, millis());
  if (!net::connectWifi(kWifiTimeoutMs)) {
    Serial.printf("[main] %s\n", net::lastError());
    showError(net::lastError());
    return;  // не блокируемся: BtnA попробует снова
  }
  showIdle();
}

void loop() {
  M5.update();

  // BtnB: тап — следующая страница (на последней — очистка),
  // долгое удержание — очистка экрана и памяти диалога.
  if (M5.BtnB.wasReleasedAfterHold()) {
    g_history.clear();
    showIdle();
  } else if (M5.BtnB.wasClicked()) {
    const size_t total = textwrap::pageCount(g_lines.size(), ui::kBodyLines);
    if (g_state == State::Showing && g_page + 1 < total) {
      ++g_page;
      ui::text(g_lines, g_page * ui::kBodyLines, kWhite);
      ui::pageIndicator(g_page, total);
    } else {
      showIdle();
    }
  }

  if ((g_state == State::Idle || g_state == State::Showing) &&
      M5.BtnA.wasPressed()) {
    runQuestion();
  }

  delay(20);
}
