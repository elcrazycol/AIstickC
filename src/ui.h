// Отрисовка экрана M5StickC PLUS2.
//
// Всё рисование собрано здесь, чтобы main.cpp оставался диспетчером
// состояний. Шрифт — сгенерированный кириллический (font_jbmono.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// Раскладка.
constexpr int kStatusH   = 14;
constexpr int kMargin    = 2;
constexpr int kBodyLines = 5;
constexpr int kPageBase  = 133;

// Полоса волн внизу экрана.
constexpr int kWaveBars = 58;   // 58*(3+1)-1 = 231 px
constexpr int kWaveH    = 30;

// Инициализация: ставит наш шрифт.
void begin();

// Полоса состояния: Wi-Fi слева, имя модели справа.
void statusBar();

// Заглавный экран: подсказка по центру, память и модель снизу.
void idle(size_t memTurns);

// Экран записи: крупный отсчёт секунд и волны громкости снизу.
// wave — массив амплитуд 0..255, свежие справа.
void recording(uint32_t remainMs, const uint8_t* wave, size_t waveN);

// Экран ожидания: радужная полоска с процентами и подписью фазы.
void progress(const char* label, int percent, uint32_t nowMs);

// Готовый ответ: окно из kBodyLines строк, начиная с from.
void text(const std::vector<std::string>& lines, size_t from, int color);

// Тонкая радужная полоска внизу с процентами (во время стрима).
void bottomBar(int percent, uint32_t nowMs);

// Индикатор страниц внизу справа.
void pageIndicator(size_t page, size_t total);

}  // namespace ui
