#include "ui.h"

#include <M5Unified.h>
#include <WiFi.h>

#include <cmath>

#include "settings.h"
#include "font_jbmono.h"

namespace ui {

namespace {

constexpr int kBlack  = lgfx::colors::TFT_BLACK;
constexpr int kRed    = lgfx::colors::TFT_RED;
constexpr int kGreen  = lgfx::colors::TFT_GREEN;
constexpr int kWhite  = lgfx::colors::TFT_WHITE;
constexpr int kGray   = 0x8410;  // нейтрально-серый RGB565

constexpr int kLineH     = kFont_jbmono_YAdvance;
constexpr int kFirstBase = 24;  // верх первой строки тела

SemaphoreHandle_t s_lock = nullptr;   // защита дисплея между задачами
volatile bool s_dotsOn = false;

// Доступ к дисплею только под мьютексом.
struct Guard {
  Guard() { if (s_lock != nullptr) xSemaphoreTake(s_lock, portMAX_DELAY); }
  ~Guard() { if (s_lock != nullptr) xSemaphoreGive(s_lock); }
};

// Ниже — только «внутренние» функции: вызывающий уже держит мьютекс.

void useFontLocked() {
  M5.Display.setFont(&kFont_jbmono);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
}

void centeredLocked(const char* s, int baseY, int color) {
  M5.Display.setTextColor(color, kBlack);
  M5.Display.setCursor((kScreenW - M5.Display.textWidth(s)) / 2, baseY);
  M5.Display.print(s);
}

// HSV -> RGB565. h: 0..255 по кругу.
uint16_t hue565(uint8_t h) {
  const uint8_t region = (uint8_t)(h / 43);
  const uint8_t rem = (uint8_t)((h - region * 43) * 6);
  uint8_t r = 0, g = 0, b = 0;
  switch (region) {
    case 0: r = 255; g = rem; b = 0; break;
    case 1: r = (uint8_t)(255 - rem); g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = rem; break;
    case 3: r = 0; g = (uint8_t)(255 - rem); b = 255; break;
    case 4: r = rem; g = 0; b = 255; break;
    default: r = 255; g = 0; b = (uint8_t)(255 - rem); break;
  }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

int rssiPercent() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  const int pct = 2 * (WiFi.RSSI() + 100);
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

void statusBarLocked() {
  M5.Display.fillRect(0, 0, kScreenW, kStatusH, kBlack);
  useFontLocked();

  char buf[24];
  snprintf(buf, sizeof(buf), "Wi-Fi %d%%", rssiPercent());
  M5.Display.setTextColor(WiFi.status() == WL_CONNECTED ? kGreen : kRed, kBlack);
  M5.Display.setCursor(kMargin, 11);
  M5.Display.print(buf);

  const char* model = "deepseek";
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kScreenW - kMargin - M5.Display.textWidth(model), 11);
  M5.Display.print(model);
}

// Волны громкости: вертикальные радужные столбики, свежие справа.
// Чистит только свою полоску — это и есть быстрый путь анимации.
void waveStripLocked(const uint8_t* wave, size_t n, uint32_t nowMs) {
  constexpr int kBarW = 3;
  constexpr int kGap = 1;
  const int totalW = kWaveBars * (kBarW + kGap) - kGap;
  const int x0 = (kScreenW - totalW) / 2;
  const int top = kScreenH - kWaveH - 1;
  const int bottom = kScreenH - 1;

  M5.Display.startWrite();
  M5.Display.fillRect(0, top, kScreenW, kWaveH + 1, kBlack);
  for (int i = 0; i < kWaveBars; ++i) {
    const int idx = (int)n - kWaveBars + i;
    const int amp = (idx >= 0 && idx < (int)n) ? wave[idx] : 0;
    if (amp <= 0) continue;
    int h = (amp * kWaveH) / 255;
    if (h < 1) h = 1;
    const uint8_t hue = (uint8_t)(i * 256 / kWaveBars + nowMs / 8);
    M5.Display.fillRect(x0 + i * (kBarW + kGap), bottom - h, kBarW, h,
                        hue565(hue));
  }
  M5.Display.endWrite();
}

// Кадр анимации точек: три точки в начале первой строки, размером со строку.
void dotsFrameLocked(uint32_t nowMs) {
  constexpr int r = 3;
  constexpr int gap = 8;
  const int cy = kFirstBase + 5;
  const int x0 = kMargin + 3;

  M5.Display.startWrite();
  M5.Display.fillRect(0, kFirstBase - 3, gap * 2 + 12, 18, kBlack);
  const float phase = (float)(nowMs % 900) / 900.0f * 6.2832f;
  for (int i = 0; i < 3; ++i) {
    const int dy = (int)(3.0f * sinf(phase + i * 2.0944f));
    const uint8_t hue = (uint8_t)(nowMs / 5 + i * 70);
    M5.Display.fillCircle(x0 + i * gap, cy + dy, r, hue565(hue));
  }
  M5.Display.endWrite();
}

// Задача анимации на втором ядре: не зависит от блокирующих сетевых вызовов.
void dotsTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(33));  // ~30 кадров в секунду
    Guard g;
    if (s_dotsOn) dotsFrameLocked(millis());
  }
}

}  // namespace

void begin() {
  useFontLocked();
  s_lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(dotsTask, "ui_dots", 4096, nullptr, 1, nullptr, 0);
}

void statusBar() {
  Guard g;
  statusBarLocked();
}

void idle(size_t memTurns) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();
  useFontLocked();

  centeredLocked("Держи A — спроси", 62, kGreen);

  char mem[32];
  snprintf(mem, sizeof(mem), "В памяти: %u", (unsigned)memTurns);
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kMargin, kScreenH - 4);
  M5.Display.print(mem);
}

void recording(uint32_t remainMs, const uint8_t* wave, size_t waveN) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();
  useFontLocked();

  // Крупный отсчёт секунд. Позицию считаем от реальной высоты шрифта,
  // чтобы не наехать на волны снизу.
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)((remainMs + 999) / 1000));
  M5.Display.setTextSize(2);
  const int h = M5.Display.fontHeight();
  const int w = M5.Display.textWidth(buf);
  M5.Display.setTextColor(kRed, kBlack);
  M5.Display.setCursor((kScreenW - w) / 2, 30 + (52 - h) / 2);
  M5.Display.print(buf);
  M5.Display.setTextSize(1);

  centeredLocked("Говори. Отпустишь — отправлю", 88, kGray);
  waveStripLocked(wave, waveN, millis());
}

void recordingWaves(const uint8_t* wave, size_t waveN) {
  Guard g;
  waveStripLocked(wave, waveN, millis());
}

void answerReset() {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();
  useFontLocked();
}

void dotsStart() {
  Guard g;
  s_dotsOn = true;
}

void dotsStop() {
  Guard g;
  s_dotsOn = false;
}

void answerText(const std::vector<std::string>& lines, size_t from, int color) {
  Guard g;
  useFontLocked();

  // Фон заливается под каждым глифом — обновление без мерцания, без
  // очистки всего экрана. Видимый префикс строк только растёт, поэтому
  // «хвостов» от прошлого кадра не остаётся.
  M5.Display.setTextColor(color, kBlack);
  M5.Display.startWrite();
  int y = kFirstBase;
  for (size_t i = from; i < lines.size() && i < from + kBodyLines; ++i) {
    M5.Display.setCursor(kMargin, y);
    M5.Display.print(lines[i].c_str());
    y += kLineH;
  }
  M5.Display.endWrite();
}

void pageIndicator(size_t page, size_t total) {
  Guard g;
  useFontLocked();
  if (total <= 1) return;
  char ind[16];
  snprintf(ind, sizeof(ind), "%u/%u", (unsigned)(page + 1), (unsigned)total);
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kScreenW - kMargin - M5.Display.textWidth(ind),
                       kPageBase);
  M5.Display.print(ind);
}

}  // namespace ui
