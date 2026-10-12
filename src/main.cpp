// Милестоуны 1–5: запись → Groq Whisper → DeepSeek → экран.
//
// Управление:
//   BtnA (зажать) — запись, отпустить — отправить
//   BtnA с экрана ответа — новый вопрос
//   BtnB (тап)    — следующая страница, на последней — очистка
//   BtnB (долгое) — очистка сразу
//
// UI: своя кириллица (JetBrains Mono, include/font_jbmono.h), перенос по
// словам, пагинация, статус-бар.

#include <Arduino.h>
#include <M5Unified.h>

#include <string>
#include <vector>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "stt.h"
#include "llm.h"
#include "font_jbmono.h"
#include "pure/wav.h"
#include "pure/text_wrap.h"

namespace {

// Константы цвета живут в lgfx::colors и НЕ подтягиваются в глобальную
// область через M5GFX.h — проверено на минимальном примере. Пишем полное
// имя, иначе сборка падает на «use of undeclared identifier 'TFT_BLACK'».
constexpr int kBlack  = lgfx::colors::TFT_BLACK;
constexpr int kRed    = lgfx::colors::TFT_RED;
constexpr int kGreen  = lgfx::colors::TFT_GREEN;
constexpr int kYellow = lgfx::colors::TFT_YELLOW;
constexpr int kWhite  = lgfx::colors::TFT_WHITE;
constexpr int kGray   = 0x8410;  // нейтрально-серый RGB565

// Раскладка. Статус-бар сверху, тело — kBodyLines строк, индикатор снизу.
constexpr int kStatusH      = 14;
constexpr int kMargin       = 2;
constexpr int kLineH        = kFont_jbmono_YAdvance;
constexpr int kFirstBase    = 26;   // базовая линия первой строки тела
constexpr int kBodyLines    = 5;
constexpr int kPageBase     = 133;  // базовая линия индикатора страниц
constexpr int kBodyWidth    = kScreenW - 2 * kMargin;

uint8_t* g_audio = nullptr;   // PSRAM: 44 байта заголовка + аудио
int16_t  g_chunk[kDmaChunkBytes / 2];

enum class State { Idle, Recording, Sending, Thinking, Showing };
State g_state = State::Idle;

std::vector<std::string> g_lines;   // текущий текст, обёрнутый по словам
size_t g_page = 0;
int    g_color = kWhite;

// Ширина любого символа в моноширинном шрифте — константа.
int measureMono(const char*, size_t, void*) { return kFont_jbmono_XAdvance; }

void useFont() {
  M5.Display.setFont(&kFont_jbmono);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
}

int rssiPercent() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  const int pct = 2 * (WiFi.RSSI() + 100);
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

void drawStatusBar() {
  const int bars = rssiPercent();
  M5.Display.fillRect(0, 0, kScreenW, kStatusH, kBlack);
  useFont();

  char buf[24];
  snprintf(buf, sizeof(buf), "Wi-Fi %d%%", bars);
  M5.Display.setTextColor(WiFi.status() == WL_CONNECTED ? kGreen : kRed, kBlack);
  M5.Display.setCursor(kMargin, 11);
  M5.Display.print(buf);

  const char* model = "deepseek";
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kScreenW - kMargin - M5.Display.textWidth(model), 11);
  M5.Display.print(model);
}

void drawBody() {
  M5.Display.fillRect(0, kStatusH, kScreenW, kScreenH - kStatusH, kBlack);
  useFont();
  M5.Display.setTextColor(g_color, kBlack);

  size_t begin = 0, end = 0;
  textwrap::pageRange(g_page, g_lines.size(), kBodyLines, begin, end);

  int y = kFirstBase;
  for (size_t i = begin; i < end; ++i) {
    M5.Display.setCursor(kMargin, y);
    M5.Display.print(g_lines[i].c_str());
    y += kLineH;
  }

  const size_t total = textwrap::pageCount(g_lines.size(), kBodyLines);
  if (total > 1) {
    char ind[16];
    snprintf(ind, sizeof(ind), "%u/%u", (unsigned)(g_page + 1),
             (unsigned)total);
    M5.Display.setTextColor(kGray, kBlack);
    M5.Display.setCursor(kScreenW - kMargin - M5.Display.textWidth(ind),
                         kPageBase);
    M5.Display.print(ind);
  }
}

void redraw() {
  drawStatusBar();
  drawBody();
}

// Показать текст с начала, обернув по словам.
void showText(const char* text, int color) {
  g_color = color;
  g_page = 0;
  textwrap::wrap(text, kBodyWidth, measureMono, nullptr, g_lines);
  redraw();
}

void showIdle() {
  g_state = State::Idle;
  showText("Готов. Держи A", kGreen);
}

// Захват audioBytes байт в PSRAM. Возвращает число записанных байт.
//
// Микрофон включается только на время записи и выключается сразу после:
// его задача I2S и DMA-прерывания конкурируют с TLS-отправкой за CPU,
// из-за чего отправка падала до ~19 КБ/с.
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

    // Кнопка отпущена — конец записи.
    M5.update();
    if (!M5.BtnA.isPressed()) break;
  }

  M5.Mic.end();
  return done * 2;
}

// Полный цикл: запись → STT → LLM → вывод.
void runQuestion() {
  if (WiFi.status() != WL_CONNECTED) {
    showText("Wi-Fi...", kYellow);
    if (!net::connectWifi(kWifiTimeoutMs)) {
      showText(net::lastError(), kRed);
      delay(2000);
      return;
    }
    redraw();
  }

  g_state = State::Recording;
  showText("Запись...", kRed);
  M5.Speaker.tone(2000, 100);

  const size_t got = record(kAudioBytes);
  const uint32_t ms = (uint32_t)(got / 2) * 1000 / kSampleRate;
  Serial.printf("[main] записано %u байт (%u мс)\n", (unsigned)got, ms);

  if (got < (size_t)kSampleRate * 2 * kMinRecordMs / 1000) {
    showText("Слишком коротко", kYellow);
    delay(1200);
    showIdle();
    return;
  }

  // Прокси на VPS принимает сырой LPCM без WAV-заголовка, поэтому
  // отправляем данные сразу за ним.
  const uint8_t* pcm = g_audio + wav::headerSize();

  g_state = State::Sending;
  showText("Отправка...", kYellow);

  stt::Result s = stt::transcribe(pcm, got);
  if (!s.ok) {
    Serial.printf("[main] ошибка STT: %s\n", s.error.c_str());
    g_state = State::Showing;
    showText(s.error.c_str(), kRed);
    return;
  }
  Serial.printf("[main] текст: %s\n", s.text.c_str());

  g_state = State::Thinking;
  showText("Думаю...", kYellow);

  llm::Reply a = llm::ask(s.text);
  g_state = State::Showing;
  if (a.ok) {
    Serial.printf("[main] ответ: %s\n", a.text.c_str());
    showText(a.text.c_str(), kWhite);
  } else {
    Serial.printf("[main] ошибка LLM: %s\n", a.error.c_str());
    showText(a.error.c_str(), kRed);
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(kScreenRotation);
  useFont();

  // Порог «долгого» нажатия BtnB — по спеке.
  M5.BtnB.setHoldThresh(kLongPressMs);

  const size_t total = wav::totalSize(kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    M5.Display.fillScreen(kBlack);
    M5.Display.setTextColor(kRed, kBlack);
    M5.Display.setCursor(kMargin, 30);
    M5.Display.print("Нет PSRAM");
    Serial.println("[main] ОШИБКА: PSRAM недоступна");
    while (true) delay(1000);
  }

  // Микрофон включается на время записи и выключается после неё
  // (см. record), поэтому здесь только проверяем, что он вообще есть.
  if (!M5.Mic.begin()) {
    M5.Display.fillScreen(kBlack);
    M5.Display.setTextColor(kRed, kBlack);
    M5.Display.setCursor(kMargin, 30);
    M5.Display.print("Микрофон не стартовал");
    while (true) delay(1000);
  }
  M5.Mic.end();

  showText("Wi-Fi...", kYellow);
  if (!net::connectWifi(kWifiTimeoutMs)) {
    showText(net::lastError(), kRed);
    Serial.printf("[main] %s\n", net::lastError());
    return;  // не блокируемся: BtnA попробует снова
  }
  showIdle();
}

void loop() {
  M5.update();

  // BtnB: тап — следующая страница (на последней — очистка),
  // долгое удержание — очистка сразу.
  if (M5.BtnB.wasReleasedAfterHold()) {
    showIdle();
  } else if (M5.BtnB.wasClicked()) {
    const size_t total = textwrap::pageCount(g_lines.size(), kBodyLines);
    if (g_state == State::Showing && g_page + 1 < total) {
      ++g_page;
      redraw();
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
