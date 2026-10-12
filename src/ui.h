// Отрисовка экрана M5StickC PLUS2.
//
// Всё рисование собрано здесь, чтобы main.cpp оставался диспетчером
// состояний. Шрифт — сгенерированный кириллический (font_jbmono.h).
//
// Единый экран ответа: полоса состояния сверху, тело (три точки, пока
// текста нет, затем сам текст). Полоска прогресса — только пока идёт
// работа; на готовом ответе её нет.
//
// Анимация: покадрово перерисовывать весь экран дорого (SPI), поэтому
// точки обновляются маленьким прямоугольником, а не fillScreen.
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
constexpr int kPageBase  = 131;  // базовая линия индикатора страниц

// Полоса волн внизу экрана записи.
constexpr int kWaveBars = 58;   // 58*(3+1)-1 = 231 px
constexpr int kWaveH    = 30;

// Инициализация: ставит наш шрифт.
void begin();

// Полоса состояния: Wi-Fi слева, имя модели справа.
void statusBar();

// Заглавный экран: подсказка по центру, память снизу.
void idle(size_t memTurns);

// Экран записи: крупный отсчёт секунд над волнами громкости.
// wave — массив амплитуд 0..255, свежие справа.
void recording(uint32_t remainMs, const uint8_t* wave, size_t waveN);

// Быстрое обновление одних волн (полный кадр при записи дорог).
void recordingWaves(const uint8_t* wave, size_t waveN);

// Полная очистка тела ответа под новую фазу (один раз на фазу).
void answerReset();

// Три точки в начале первой строки, размером со строку. Дёшево: чистит
// только свою полоску, поэтому годится для анимации покадрово.
void answerDots(uint32_t nowMs);

// Текст ответа: окно из kBodyLines строк, начиная с from.
void answerText(const std::vector<std::string>& lines, size_t from, int color);

// Радужная полоска прогресса внизу экрана. chip — подпись справа
// («Отправка 40%»); nullptr — без подписи.
void bottomBar(int percent, const char* chip);

// Индикатор страниц внизу справа (на готовом ответе).
void pageIndicator(size_t page, size_t total);

}  // namespace ui
