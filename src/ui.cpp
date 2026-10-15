#include "ui.h"

#include <M5Unified.h>
#include <WiFi.h>

#include <cmath>

#include "settings.h"
#include "font_jbmono.h"
#include "font_small.h"
#include "idle_image.h"
#include "pure/text_wrap.h"

namespace ui {

namespace {

constexpr int kBlack  = lgfx::colors::TFT_BLACK;
constexpr int kRed    = lgfx::colors::TFT_RED;
constexpr int kGreen  = lgfx::colors::TFT_GREEN;
constexpr int kYellow = lgfx::colors::TFT_YELLOW;
constexpr int kWhite  = lgfx::colors::TFT_WHITE;
constexpr int kGray   = 0x8410;  // нейтрально-серый RGB565
constexpr int kOrange = lgfx::colors::TFT_ORANGE;
// Блок кода: тёмная подложка, зелёная полоска слева и мягкий зелёный текст —
// так код отличается от обычного текста, не споря с палитрой.
constexpr int kCodeBg   = 0x10A2;  // очень тёмный зелёный
constexpr int kCodeEdge = 0x2E68;  // полоска слева
constexpr int kCodeText = 0x9F51;  // светлый зелёный

constexpr int kLineH     = kFont_jbmono_YAdvance;
constexpr int kFirstBase = 26;  // верх первой строки тела

SemaphoreHandle_t s_lock = nullptr;   // защита дисплея между задачами
volatile bool s_dotsOn = false;
int s_dotsRow = 0;  // на какую строку тела рисовать точки
uint8_t s_brightness = 255;           // исходная яркость подсветки

// Доступ к дисплею только под мьютексом.
//
// Мьютекс рекурсивный: если «внешняя» функция под замком позовёт другую
// «внешнюю», задача возьмёт замок второй раз и не встанет намертво. Именно на
// этом однажды повесился экран настроек (statusBar внутри list) — лучше
// подстраховаться, чем ловить чёрный экран.
struct Guard {
  Guard() { if (s_lock != nullptr) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
  ~Guard() { if (s_lock != nullptr) xSemaphoreGiveRecursive(s_lock); }
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

// Уровень заряда: читаем не чаще раза в 5 с — запрос к питанию на каждый кадр
// заметно тормозит отрисовку.
//
// Признака зарядки здесь нет и быть не может: у M5StickC PLUS2 убрали AXP192,
// батарея меряется через ADC, и M5Unified честно возвращает charge_unknown.
// Определять заряд по наклону напряжения пробовали — наклон в единицы мВ в
// минуту при шуме ADC в ±10 мВ, то есть значок загорался бы через минуты.
// Такой индикатор хуже, чем никакого, поэтому его нет.
int g_powerPct = -1;
uint32_t g_powerRead = 0;

int powerPctLocked() {
  const uint32_t now = millis();
  if (g_powerPct < 0 || now - g_powerRead > 5000) {
    g_powerRead = now;
    g_powerPct = (int)M5.Power.getBatteryLevel();
  }
  return g_powerPct;
}

void statusBarDrawLocked(bool clear) {
  if (clear) M5.Display.fillRect(0, 0, kScreenW, kStatusH, kBlack);

  // Батарея справа, низ — на одном уровне с палочками.
  const int pct = powerPctLocked();

  constexpr int bw = 15;
  constexpr int bh = 8;
  constexpr uint16_t kOutline = 0x4208;  // тёмно-серый, обводка не бросается
  const int bx = kScreenW - kMargin - bw - 2;  // 2 — носик
  const int by = 13 - bh;

  M5.Display.drawRect(bx, by, bw, bh, kOutline);
  M5.Display.fillRect(bx + bw, by + 2, 2, 4, kOutline);

  int lvl = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  const int inner = (bw - 2) * lvl / 100;
  if (inner > 0) {
    const uint16_t c = lvl <= 20 ? kRed : (lvl <= 50 ? kYellow : kGreen);
    M5.Display.fillRect(bx + 1, by + 1, inner, bh - 2, c);
  }

  // Палочки сигнала левее батареи.
  const int level = (rssiPercent() * 4 + 99) / 100;
  constexpr int sbw = 2;
  constexpr int sgap = 2;
  const int sx0 = bx - 8 - (4 * sbw + 3 * sgap);
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + i * 2;  // 3,5,7,9
    const uint16_t color = (i < level) ? kGreen : 0x2104;
    M5.Display.fillRect(sx0 + i * (sbw + sgap), 13 - h, sbw, h, color);
  }
}

void statusBarLocked() { statusBarDrawLocked(true); }


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
  const int top = kFirstBase + s_dotsRow * kLineH;
  // По центру строки: раньше точки стояли у самого верха и почти
  // прилипали к надписи инструмента.
  const int cy = top + kLineH / 2;
  const int x0 = kMargin + 3;

  M5.Display.startWrite();
  M5.Display.fillRect(0, top, gap * 2 + 12, kLineH, kBlack);
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
#ifdef AISTICK_DOTS_TEST
    static uint32_t frames = 0;
    if (s_dotsOn) {
      if ((++frames % 10) == 0) {
        Serial.printf("[dots] кадр %u, идёт=%d\n", (unsigned)frames,
                      (int)s_dotsOn);
      }
      dotsFrameLocked(millis());
    }
#else
    if (s_dotsOn) dotsFrameLocked(millis());
#endif
  }
}

}  // namespace

// Экран веб-панели: выключатель, состояние и QR, когда панель включена.
void panel(bool on, bool used, const char* url, const char* ip,
           const char* code) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();
  useFontLocked();

  if (!on) {
    // Выключена: это и есть главный ответ экрана, поэтому крупно и красным.
    M5.Display.setTextColor(kRed, kBlack);
    M5.Display.setCursor(12, 40);
    M5.Display.print("Панель выключена");
    M5.Display.setTextColor(kGray, kBlack);
    M5.Display.setCursor(12, 66);
    M5.Display.print("кнопка A — включить");
    M5.Display.setCursor(12, 90);
    M5.Display.print("удержание B — назад");
    return;
  }

  // Включена: QR рисует библиотека — белым с чёрными модулями и тихой зоной,
  // иначе сканер его не прочтёт.
  constexpr int qs = 112;
  constexpr int qx = 4;
  constexpr int qy = 16;
  M5.Display.qrcode(url, qx, qy, qs, 1, true);

  const int tx = qx + qs + 8;
  M5.Display.setTextColor(kGreen, kBlack);
  M5.Display.setCursor(tx, 18);
  M5.Display.print("Веб-панель");
  M5.Display.setTextColor(kWhite, kBlack);
  M5.Display.setCursor(tx, 38);
  M5.Display.print(ip);
  M5.Display.setTextColor(kYellow, kBlack);
  M5.Display.setCursor(tx, 58);
  M5.Display.print("код ");
  M5.Display.print(code);
  // Зелёный — кто-то на странице, жёлтый — включена, но никто не заходил.
  M5.Display.setTextColor(used ? kGreen : kYellow, kBlack);
  M5.Display.setCursor(tx, 78);
  M5.Display.print(used ? "на связи" : "ждём");
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(tx, 98);
  M5.Display.print("A — выкл.");
}

void begin() {
  useFontLocked();
  s_brightness = M5.Display.getBrightness();
  s_lock = xSemaphoreCreateRecursiveMutex();
  xTaskCreatePinnedToCore(dotsTask, "ui_dots", 4096, nullptr, 1, nullptr, 0);
}

void statusBar() {
  Guard g;
  statusBarLocked();
}

void idle(size_t memTurns, uint32_t msgs) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  useFontLocked();

  // Картинка прижата к правому низу, снизу без отступа, сверху небольшой.
  M5.Display.pushImage(kScreenW - kImage_idle_Width,
                       kScreenH - kImage_idle_Height, kImage_idle_Width,
                       kImage_idle_Height,
                       (const lgfx::rgb565_t*)kImage_idle_Bits);

  // Иконки статуса поверх картинки — фон не затираем, чтобы картинка
  // доходила до самого верха.
  statusBarDrawLocked(false);

  // Слева — «шапка» как у репозитория.
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kMargin, 40);
  M5.Display.print("elcrazycol/");

  M5.Display.setTextColor(kGreen, kBlack);
  M5.Display.setCursor(kMargin, 60);
  M5.Display.print("AIstickC");

  char buf[24];
  snprintf(buf, sizeof(buf), "%u msgs", (unsigned)msgs);
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kMargin, 80);
  M5.Display.print(buf);

  char mem[32];
  snprintf(mem, sizeof(mem), "В памяти: %u", (unsigned)memTurns);
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
  s_dotsRow = 0;  // обычное ожидание — точки с первой строки
  s_dotsOn = true;
#ifdef AISTICK_DOTS_TEST
  Serial.println("[dots] старт");
#endif
}

void dotsStop() {
  Guard g;
  s_dotsOn = false;
#ifdef AISTICK_DOTS_TEST
  Serial.println("[dots] стоп");
#endif
}

void answerLines(const std::vector<Line>& lines, size_t pageFirst, size_t from,
                 size_t to, int color, size_t noteLine) {
  Guard g;
  useFontLocked();

  // Фон заливается под каждым глифом — обновление без мерцания, без
  // очистки всего экрана. Видимый префикс строк только растёт, поэтому
  // «хвостов» от прошлого кадра не остаётся.
  M5.Display.startWrite();
  for (size_t i = from; i < to && i < lines.size() &&
                     i < pageFirst + kBodyLines; ++i) {
    const int y = kFirstBase + (int)(i - pageFirst) * kLineH;
    const bool note = (i == noteLine);
    const bool code = lines[i].code;
    // Пометка инструмента — подпись, а не текст: мелким шрифтом и серым,
    // как надпись «ищу в интернете», чтобы её не путали с ответом.
    // Строки кода — мелким шрифтом на тёмной подложке с полоской слева.
    if (note) {
      M5.Display.setFont(&kFont_small);
      M5.Display.setTextColor(kGray, kBlack);
      M5.Display.setCursor(kMargin, y + 2);
    } else if (code) {
      M5.Display.fillRect(0, y - 1, kScreenW, kLineH - 2, kCodeBg);
      M5.Display.fillRect(0, y - 1, 3, kLineH - 2, kCodeEdge);
      M5.Display.setFont(&kFont_small);
      M5.Display.setTextColor(kCodeText, kCodeBg);
      M5.Display.setCursor(kMargin + 6, y + 2);
    } else {
      M5.Display.setFont(&kFont_jbmono);
      M5.Display.setTextColor(color, kBlack);
      M5.Display.setCursor(kMargin, y);
    }
    M5.Display.print(lines[i].text.c_str());
  }
  useFontLocked();  // вернуть основной шрифт
  M5.Display.endWrite();
}

void answerText(const std::vector<Line>& lines, size_t from, int color) {
  answerLines(lines, from, from, from + kBodyLines, color);
}

void pageIndicator(size_t page, size_t total) {
  Guard g;
  useFontLocked();

  char ind[16];
  snprintf(ind, sizeof(ind), "%u/%u", (unsigned)(page + 1), (unsigned)total);

  // Верхний левый угол: в полосе состояния слева пусто (батарея и палочки
  // справа), поэтому счётчику там никто не мешает. Полоску под ним чистим:
  // иначе от прошлого числа останутся хвосты, когда оно станет короче.
  constexpr int kClearW = 6 * kFont_jbmono_XAdvance;  // с запасом на «12/12»
  M5.Display.fillRect(0, 0, kClearW, kStatusH, kBlack);

  // M5GFX считает cursor_y верхом строки текста, а не базовой линией, поэтому
  // вместо подбора числа ставим датум по центру: цифры ложатся ровно в строку
  // иконок (4..13) и не поедут, если метрики шрифта поменяются.
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(kGray, kBlack);
  M5.Display.setCursor(kMargin, kPageBase);
  M5.Display.print(ind);
  M5.Display.setTextDatum(top_left);
}

void toolWait(const char* label) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();
  useFontLocked();

  const bool hasLabel = (label != nullptr && label[0] != 0);
  // С меткой точки уходят на строку ниже, чтобы не наезжать на неё.
  s_dotsRow = hasLabel ? 1 : 0;
  if (hasLabel) {
    M5.Display.setTextColor(kGray, kBlack);
    M5.Display.setCursor(kMargin, kFirstBase);
    M5.Display.print(label);
  }
  s_dotsOn = true;
}

// Ширина любого символа в моноширинном шрифте — константа.
int measureNotice(const char*, size_t, void*) {
  return kFont_jbmono_XAdvance;
}

void notice(const char* text) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();
  useFontLocked();

  // Сообщение бывает длиннее экрана: «не поставить: нет связи с
  // raw.githubusercontent.com» — это 50 знаков, а в строку влезает 29.
  // Раньше текст рисовался одной линией по центру и обрезался с двух сторон
  // вместе с самой полезной частью — например, с подсказкой про память.
  // Поэтому переносим и подгоняем рамку под самую широкую строку.
  std::vector<std::string> lines;
  textwrap::wrap(text, kScreenW - 16, measureNotice, nullptr, lines);
  if (lines.empty()) return;
  constexpr size_t kMaxNoticeLines = 3;
  if (lines.size() > kMaxNoticeLines) {
    lines.resize(kMaxNoticeLines);
    lines.back() += "…";
  }

  int widest = 0;
  for (const std::string& s : lines) {
    const int w = (int)s.size() * kFont_jbmono_XAdvance;
    if (w > widest) widest = w;
  }
  const int bw = widest + 12;
  const int bh = (int)lines.size() * kLineH + 10;
  const int bx = (kScreenW - bw) / 2;
  const int by = (kScreenH - bh) / 2;

  M5.Display.fillRect(bx, by, bw, bh, 0x18E3);  // тёмная подложка
  M5.Display.drawRect(bx, by, bw, bh, kGray);
  M5.Display.setTextColor(kWhite, 0x18E3);
  for (size_t i = 0; i < lines.size(); ++i) {
    M5.Display.setCursor(bx + 6, by + 5 + (int)i * kLineH);
    M5.Display.print(lines[i].c_str());
  }
}

void list(const char* title, const std::vector<ListItem>& items,
          size_t index) {
  Guard g;
  M5.Display.fillScreen(kBlack);
  statusBarLocked();  // не statusBar(): он берёт тот же мьютекс и вешает задачу
  useFontLocked();

  constexpr int kSelBg  = 0x2128;  // тёмно-синяя подложка выбранной строки
  constexpr int kDotX   = kScreenW - 13;
  constexpr int kStripe = 3;       // акцентная полоска слева

  M5.Display.setTextColor(kYellow, kBlack);
  M5.Display.setCursor(kMargin, 24);
  M5.Display.print(title);

  constexpr size_t kVisible = 4;  // строк под заголовком
  size_t first = 0;
  if (items.size() > kVisible && index >= kVisible) {
    first = index - kVisible + 1;
  }

  int y = 44;
  for (size_t i = first; i < items.size() && i < first + kVisible; ++i) {
    const bool sel = (i == index);
    const int bg = sel ? kSelBg : kBlack;

    if (sel) {
      // Подсветка — во всю строку и встык со следующей, плюс акцентная
      // полоска слева. Фон текста обязательно тот же: иначе буквы прорезают
      // в подложке чёрные клетки.
      M5.Display.fillRect(0, y, kScreenW, kLineH, kSelBg);
      M5.Display.fillRect(0, y, kStripe, kLineH, kYellow);
    }

    M5.Display.setTextColor(sel ? kWhite : kGray, bg);
    M5.Display.setCursor(kMargin + 6, y);
    M5.Display.print(items[i].text.c_str());

    // Правое поле: значение настройки. Метки-точки и значение в одной строке
    // не встречаются, поэтому делят одно место.
    if (!items[i].value.empty()) {
      const int w = M5.Display.textWidth(items[i].value.c_str());
      M5.Display.setTextColor(sel ? kYellow : 0xC618, bg);
      M5.Display.setCursor(kScreenW - kMargin - w, y);
      M5.Display.print(items[i].value.c_str());
    }

    // Метка справа — точками: они чётче любых знаков из шрифта.
    const int cy = y + 8;
    switch (items[i].mark) {
      case Mark::Installed:
        M5.Display.fillCircle(kDotX, cy, 3, kGreen);
        break;
      case Mark::Update:
        M5.Display.drawCircle(kDotX, cy, 6, kOrange);
        M5.Display.fillCircle(kDotX, cy, 3, kOrange);
        break;
      case Mark::None:
      default:
        break;
    }
    y += kLineH;
  }

  if (items.empty()) {
    M5.Display.setTextColor(kGray, kBlack);
    M5.Display.setCursor(kMargin, 44);
    M5.Display.print("пусто");
  }
}

void holdProgress(int percent) {
  Guard g;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  constexpr int h = 3;
  const int y = kScreenH - h;
  M5.Display.fillRect(0, y, kScreenW, h, kBlack);
  const int w = kScreenW * percent / 100;
  if (w > 0) M5.Display.fillRect(0, y, w, h, kYellow);
}

void screenOff() {
  Guard g;
  M5.Display.setBrightness(0);
}

void screenOn() {
  Guard g;
  M5.Display.setBrightness(s_brightness);
}

}  // namespace ui
