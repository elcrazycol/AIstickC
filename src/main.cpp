// Милестоун 1: запись → Groq Whisper → текст на экране.
//
// Управление:
//   BtnA (зажать) — запись, отпустить — отправить
//   BtnB (тап)    — очистить экран
//
// Шрифт пока встроенный: моноширинный с кириллицей — милестоун 5.

#include <Arduino.h>
#include <M5Unified.h>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "stt.h"
#include "pure/wav.h"

namespace {

// Константы цвета живут в lgfx::colors и НЕ подтягиваются в глобальную
// область через M5GFX.h — проверено на минимальном примере. Пишем полное
// имя, иначе сборка падает на «use of undeclared identifier 'TFT_BLACK'».
constexpr int kBlack  = lgfx::colors::TFT_BLACK;
constexpr int kRed    = lgfx::colors::TFT_RED;
constexpr int kGreen  = lgfx::colors::TFT_GREEN;
constexpr int kYellow = lgfx::colors::TFT_YELLOW;
constexpr int kWhite  = lgfx::colors::TFT_WHITE;

uint8_t* g_audio = nullptr;   // PSRAM: 44 байта заголовка + аудио
int16_t  g_chunk[kDmaChunkBytes / 2];

enum class State { Idle, Recording, Sending, Showing };
State g_state = State::Idle;
String g_shown;

void showStatus(const char* text, int color) {
  M5.Display.fillScreen(kBlack);
  M5.Display.setTextColor(color, kBlack);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 4);
  M5.Display.print(text);
}

// ВНИМАНИЕ: LGFXBase объявляет `using Print::write`, но не `using
// Print::print`. Поэтому print(const String&) скрыт, и String сюда
// передавать нельзя. Печатаем через c_str().
void showWrapped(const char* text, int color) {
  M5.Display.fillScreen(kBlack);
  M5.Display.setTextColor(color, kBlack);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 4);
  M5.Display.print(text);   // перенос по словам добавим в милестоуне 5
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

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(kScreenRotation);
  M5.Display.setTextWrap(true);

  const size_t total = wav::totalSize(kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    showStatus("Нет PSRAM", kRed);
    Serial.println("[main] ОШИБКА: PSRAM недоступна");
    while (true) delay(1000);
  }

  // Микрофон включается на время записи и выключается после неё
  // (см. record), поэтому здесь только проверяем, что он вообще есть.
  if (!M5.Mic.begin()) {
    showStatus("Микрофон не стартовал", kRed);
    while (true) delay(1000);
  }
  M5.Mic.end();

  showStatus("Wi-Fi...", kYellow);
  if (!net::connectWifi(kWifiTimeoutMs)) {
    showStatus(net::lastError(), kRed);
    Serial.printf("[main] %s\n", net::lastError());
    return;  // не блокируемся: BtnA попробует снова
  }
  // Строку про Wi-Fi печатает net::connectWifi (там же RSSI).
  showStatus("Ready. Держи A", kGreen);
}

void loop() {
  M5.update();

  if (M5.BtnB.wasPressed()) {
    g_shown = "";
    g_state = State::Idle;
    showStatus("Ready. Держи A", kGreen);
  }

  if (g_state == State::Idle && M5.BtnA.wasPressed()) {
    if (WiFi.status() != WL_CONNECTED) {
      showStatus("Wi-Fi...", kYellow);
      if (!net::connectWifi(kWifiTimeoutMs)) {
        showStatus(net::lastError(), kRed);
        delay(2000);
        return;
      }
    }

    g_state = State::Recording;
    showStatus("RECORDING...", kRed);
    M5.Speaker.tone(2000, 100);

    const size_t got = record(kAudioBytes);
    const uint32_t ms = (uint32_t)(got / 2) * 1000 / kSampleRate;
    Serial.printf("[main] записано %u байт (%u мс)\n", (unsigned)got, ms);

    if (got < (size_t)kSampleRate * 2 * kMinRecordMs / 1000) {
      showStatus("Слишком коротко", kYellow);
      delay(1500);
      g_state = State::Idle;
      showStatus("Ready. Держи A", kGreen);
      return;
    }

    // SpeechKit принимает сырой LPCM без WAV-заголовка, поэтому
    // отправляем данные сразу за ним. Ограничения сервиса: 1 МБ и 30 с —
    // наши 8 с (256 КБ) проходят с запасом.
    const uint8_t* pcm = g_audio + wav::headerSize();

    g_state = State::Sending;
    showStatus("Sending...", kYellow);

    stt::Result r = stt::transcribe(pcm, got);
    if (r.ok) {
      g_shown = r.text;
      showWrapped(g_shown.c_str(), kWhite);
      Serial.printf("[main] текст: %s\n", g_shown.c_str());
    } else {
      g_shown = r.error;
      showWrapped(g_shown.c_str(), kRed);
      Serial.printf("[main] ошибка: %s\n", g_shown.c_str());
    }
    g_state = State::Showing;
  }

  delay(20);
}
