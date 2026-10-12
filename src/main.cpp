// Карманный голосовой ассистент: запись → Whisper → DeepSeek → экран.
//
// Управление:
//   BtnA (зажать)  — запись, отпусти — отправить
//   BtnB (тап)     — следующая страница (закольцовано)
//   BtnB (3 с)     — сброс сессии (память + главный экран) с панелью
//
// Состояния: Idle → Recording → Sending → Recognizing → Streaming → Showing.
// Экран один: полоса состояния и тело. Пока идёт работа — три перетекающие
// точки (анимирует отдельная задача), затем по мере прихода печатается текст.
// Всё рисование — в ui.cpp.

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

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

constexpr int kWhite = lgfx::colors::TFT_WHITE;
constexpr int kRed   = lgfx::colors::TFT_RED;

constexpr int kBodyW = kScreenW - 2 * ui::kMargin;

uint8_t* g_audio = nullptr;   // PSRAM: заголовок + аудио
int16_t  g_chunk[kDmaChunkBytes / 2];

enum class State { Idle, Recording, Sending, Recognizing, Streaming, Showing };
State g_state = State::Idle;

std::vector<std::string> g_lines;
size_t g_page = 0;
int    g_color = kWhite;

chat::History g_history(kChatMaxTurns);

// Счётчик отправленных сообщений: живёт в NVS, переживает выключение.
Preferences g_prefs;
uint32_t g_msgs = 0;

// Волны: амплитуды последних кусков записи, свежие справа.
uint8_t g_wave[ui::kWaveBars] = {0};
size_t  g_waveN = 0;

uint32_t g_lastDraw = 0;     // троттлинг перерисовок текста
String   g_stream;           // накопленный текст ответа
bool     g_textStarted = false;

// Удержание боковой кнопки.
uint32_t g_btnStart = 0;
bool     g_btnArmed = false;  // нажатие ещё не отработано

// Ширина любого символа в моноширинном шрифте — константа.
int measureMono(const char*, size_t, void*) { return kFont_jbmono_XAdvance; }

void wrapInto(const char* text, std::vector<std::string>& out) {
  textwrap::wrap(text, kBodyW, measureMono, nullptr, out);
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

// Экран ответа: текст + индикатор страниц.
void drawAnswer(size_t page) {
  ui::dotsStop();
  ui::answerReset();
  ui::answerText(g_lines, page * ui::kBodyLines, g_color);
  ui::pageIndicator(page, textwrap::pageCount(g_lines.size(), ui::kBodyLines));
}

void showError(const char* text) {
  g_state = State::Showing;
  g_color = kRed;
  g_page = 0;
  wrapInto(text, g_lines);
  drawAnswer(g_page);
}

void showIdle() {
  ui::dotsStop();
  g_state = State::Idle;
  ui::idle(g_history.turnCount(), g_msgs);
}

// Начало фазы ожидания: чистый экран и запущенная анимация точек.
void startWaiting() {
  g_textStarted = false;
  g_lastDraw = 0;
  ui::answerReset();
  ui::dotsStart();
}

// Кусок ответа пришёл: копим текст и печатаем его без мерцания.
void onDelta(const char* piece, size_t len, size_t, void*) {
  if (len == 0) return;  // только reasoning — точки продолжают идти

  g_stream.concat(piece);
  const uint32_t now = millis();
  if (now - g_lastDraw < 50) return;
  g_lastDraw = now;

  wrapInto(g_stream.c_str(), g_lines);
  if (!g_textStarted) {
    g_textStarted = true;
    ui::dotsStop();
    ui::answerReset();  // стереть точки перед первым текстом
  }
  ui::answerText(g_lines, 0, kWhite);
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
  uint32_t lastSec = 0xFFFFFFFF;
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
    if (millis() - g_lastDraw > 40) {
      g_lastDraw = millis();
      const uint32_t remain =
          elapsed >= kMaxRecordMs ? 0 : kMaxRecordMs - elapsed;
      const uint32_t sec = (remain + 999) / 1000;
      if (sec != lastSec) {
        lastSec = sec;
        ui::recording(remain, g_wave, g_waveN);  // полный кадр раз в секунду
      } else {
        ui::recordingWaves(g_wave, g_waveN);     // дешёвая перерисовка волн
      }
    }
    if (!M5.BtnA.isPressed()) break;
  }

  M5.Mic.end();
  return done * 2;
}

// Полный цикл: запись → STT → LLM → вывод.
void runQuestion() {
  if (WiFi.status() != WL_CONNECTED) {
    startWaiting();
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
    showError("Слишком коротко");
    delay(1200);
    showIdle();
    return;
  }

  // Прокси на VPS принимает сырой LPCM без WAV-заголовка.
  const uint8_t* pcm = g_audio + wav::headerSize();

  g_state = State::Sending;
  startWaiting();

  g_state = State::Recognizing;
  stt::Result s = stt::transcribe(pcm, got);
  if (!s.ok) {
    Serial.printf("[main] ошибка STT: %s\n", s.error.c_str());
    showError(s.error.c_str());
    return;
  }
  Serial.printf("[main] текст: %s\n", s.text.c_str());

  // Сообщение отправляем — увеличиваем счётчик и сохраняем в NVS.
  ++g_msgs;
  g_prefs.putUInt("msgs", g_msgs);

  // Вопрос распознан — спрашиваем модель потоком.
  g_state = State::Streaming;
  g_stream = "";
  startWaiting();

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
    g_color = kWhite;
    g_page = 0;
    wrapInto(a.text.c_str(), g_lines);
    drawAnswer(g_page);
  } else {
    Serial.printf("[main] ошибка LLM: %s\n", a.error.c_str());
    showError(a.error.c_str());
  }
}

// Очистка сессии с уведомлением на экране.
void clearSession() {
  g_history.clear();
  Serial.println("[main] сессия сброшена");
  ui::notice("История разговора очищена");
  delay(1500);
  showIdle();
}

// Боковая кнопка: тап — листание вперёд, удержание 3 с — сброс сессии
// (панель + главный экран). Срабатывает, не дожидаясь отпускания.
void pollSideButton() {
  if (M5.BtnB.wasPressed()) {
    g_btnStart = millis();
    g_btnArmed = true;
  }

  if (g_btnArmed && M5.BtnB.isPressed() &&
      (millis() - g_btnStart) >= kClearHoldMs) {
    g_btnArmed = false;
    Serial.println("[main] удержание: сброс сессии");
    clearSession();
  }

  if (M5.BtnB.wasReleased()) {
    const uint32_t held = millis() - g_btnStart;
    if (g_btnArmed && held < kClearHoldMs) {
      // Короткий тап — листание вперёд, закольцованно.
      const size_t total = textwrap::pageCount(g_lines.size(), ui::kBodyLines);
      if (g_state == State::Showing && total > 1) {
        g_page = (g_page + 1) % total;
        Serial.printf("[main] страница %u/%u\n", (unsigned)(g_page + 1),
                      (unsigned)total);
        drawAnswer(g_page);
      }
    }
    g_btnArmed = false;
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

  // Счётчик сообщений переживает выключение.
  g_prefs.begin("aistick", false);
  g_msgs = g_prefs.getUInt("msgs", 0);
  Serial.printf("[main] всего сообщений: %u\n", (unsigned)g_msgs);

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

  startWaiting();
  if (!net::connectWifi(kWifiTimeoutMs)) {
    Serial.printf("[main] %s\n", net::lastError());
    showError(net::lastError());
    return;  // не блокируемся: BtnA попробует снова
  }
  showIdle();
}

void loop() {
  M5.update();

  // Боковая кнопка: тап — листание вперёд, удержание — главный/сброс.
  pollSideButton();

  if ((g_state == State::Idle || g_state == State::Showing) &&
      M5.BtnA.wasPressed()) {
    runQuestion();
  }

  delay(20);
}
