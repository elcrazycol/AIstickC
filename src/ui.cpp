#include "ui.h"

#include <M5Unified.h>
#include <WiFi.h>

#include "settings.h"
#include "font_jbmono.h"

namespace ui {

namespace {

constexpr int kBlack  = lgfx::colors::TFT_BLACK;
constexpr int kRed    = lgfx::colors::TFT_RED;
constexpr int kGreen  = lgfx::colors::TFT_GREEN;
constexpr int kYellow = lgfx::colors::TFT_YELLOW;
constexpr int kWhite  = lgfx::colors::TFT_WHITE;
constexpr int kGray   = 0x8410;  // нейтрально-серый RGB565

constexpr int kLineH     = kFont_jbmono_YAdvance;
constexpr int kFirstBase = 26;  // базовая линия первой строки тела
constexpr int kBodyWidth = kScreenW - 2 * kMargin;

void useFont() {
  M5.Display.setFont(&kFont_jbmono);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
}

// Текст по центру на базовой линии baseY.
void centered(const char* s, int baseY, int color) {
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

// Волны громкости: вертикальные радужные столбики, свежие справа.
void drawWave(const uint8_t* wave, size_t n, uint32_t nowMs) {
  constexpr int kBarW = 3;
  constexpr int kGap = 1;
  const int totalW = kWaveBars * (kBarW + kGap) - kGap;
  const int x0 = (kScreenW - totalW) / 2;
  const int bottom = kScreenH - 1;

  M5.Display.drawFastHLine(x0, kScreenH - kWaveH - 1, totalW, 0x2104);
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
}

}  // namespace

void begin() { useFont(); }

void statusBar() {
  M5.Display.fillRect(0, 0, kScreenW, kStatusH, kBlack);
  useFont();

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

void idle(size_t memTurns) {
  M5.Display.fillScreen(kBlack);
  statusBar();
  useFont();

  centered("Держи A — спроси", 62, kGreen);

  char mem[32];
  snprintf(mem, sizeof(mem), "В памяти: %u", (unsigned)memTurns);
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kMargin, kScreenH - 4);
  M5.Display.print(mem);
}

void recording(uint32_t remainMs, const uint8_t* wave, size_t waveN) {
  M5.Display.fillScreen(kBlack);
  statusBar();
  useFont();

  // Крупный отсчёт секунд.
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)((remainMs + 999) / 1000));
  M5.Display.setTextSize(3);
  const int w = M5.Display.textWidth(buf);
  M5.Display.setTextColor(kRed, kBlack);
  M5.Display.setCursor((kScreenW - w) / 2, 62);
  M5.Display.print(buf);
  M5.Display.setTextSize(1);

  centered("Говори. Отпустишь — отправлю", 86, kGray);
  drawWave(wave, waveN, millis());
}

void progress(const char* label, int percent, uint32_t nowMs) {
  M5.Display.fillScreen(kBlack);
  statusBar();
  useFont();

  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  centered(label, 44, kYellow);

  constexpr int kBarH = 18;
  const int barX = kMargin + 1;
  const int barW = kScreenW - 2 * (kMargin + 1);
  const int barY = 58;

  M5.Display.drawRect(barX - 1, barY - 1, barW + 2, kBarH + 2, kGray);

  const int fill = (percent * barW) / 100;
  for (int x = 0; x < fill; ++x) {
    const uint8_t hue = (uint8_t)(x * 256 / barW + nowMs / 6);
    M5.Display.drawFastVLine(barX + x, barY, kBarH, hue565(hue));
  }

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  M5.Display.setTextSize(2);
  const int w = M5.Display.textWidth(pct);
  M5.Display.setTextColor(kWhite, kBlack);
  M5.Display.setCursor((kScreenW - w) / 2, 100);
  M5.Display.print(pct);
  M5.Display.setTextSize(1);
}

void text(const std::vector<std::string>& lines, size_t from, int color) {
  M5.Display.fillScreen(kBlack);
  statusBar();
  useFont();

  M5.Display.setTextColor(color, kBlack);
  int y = kFirstBase;
  for (size_t i = from; i < lines.size() && i < from + kBodyLines; ++i) {
    M5.Display.setCursor(kMargin, y);
    M5.Display.print(lines[i].c_str());
    y += kLineH;
  }
}

void bottomBar(int percent, uint32_t nowMs) {
  useFont();
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  constexpr int kBarH = 8;
  const int barX = kMargin;
  const int barW = kScreenW - 2 * kMargin;
  const int barY = kScreenH - kBarH - 1;

  const int fill = (percent * barW) / 100;
  for (int x = 0; x < fill; ++x) {
    const uint8_t hue = (uint8_t)(x * 256 / barW + nowMs / 6);
    M5.Display.drawFastVLine(barX + x, barY, kBarH, hue565(hue));
  }

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  const int w = M5.Display.textWidth(pct);
  const int tx = kScreenW - kMargin - w;
  M5.Display.fillRect(tx - 1, barY - 9, w + 2, 9, kBlack);
  M5.Display.setTextColor(kWhite, kBlack);
  M5.Display.setCursor(tx, barY - 2);
  M5.Display.print(pct);
}

void pageIndicator(size_t page, size_t total) {
  useFont();
  if (total <= 1) return;
  char ind[16];
  snprintf(ind, sizeof(ind), "%u/%u", (unsigned)(page + 1), (unsigned)total);
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kScreenW - kMargin - M5.Display.textWidth(ind),
                       kPageBase);
  M5.Display.print(ind);
}

}  // namespace ui
