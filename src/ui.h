// Отрисовка экрана M5StickC PLUS2.
//
// Всё рисование собрано здесь, чтобы main.cpp оставался диспетчером
// состояний. Шрифт — сгенерированный кириллический (font_jbmono.h).
//
// Единый экран ответа: полоса состояния, тело (три перетекающие точки, пока
// текста нет, затем сам текст). Никакой полоски прогресса — только точки и
// текст.
//
// Точки анимирует отдельная задача на втором ядре: сетевые вызовы
// (TLS-рукопожатие, ожидание ответа) блокирующие, и при анимации «из цикла»
// точки замирали. Доступ к дисплею из двух задач защищён мьютексом.
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
constexpr int kPageBase  = 13;   // базовая линия счётчика страниц (по иконкам)

// Полоса волн внизу экрана записи.
constexpr int kWaveBars = 58;   // 58*(3+1)-1 = 231 px
constexpr int kWaveH    = 30;

// Инициализация: шрифт, мьютекс дисплея и задача анимации точек.
void begin();

// Полоса состояния: Wi-Fi слева, имя модели справа.
void statusBar();

// Заглавный экран: картинка справа, слева подпись и счётчики.
void idle(size_t memTurns, uint32_t msgs);

// Экран записи: крупный отсчёт секунд над волнами громкости.
// wave — массив амплитуд 0..255, свежие справа.
void recording(uint32_t remainMs, const uint8_t* wave, size_t waveN);

// Быстрое обновление одних волн (полный кадр при записи дорог).
void recordingWaves(const uint8_t* wave, size_t waveN);

// Полная очистка тела под новую фазу.
void answerReset();

// Анимация точек: включить/выключить. Пока включена, задача рисует кадры
// независимо от сети.
void dotsStart();
void dotsStop();

// Текст ответа: окно из kBodyLines строк, начиная с from. Рисуется с заливкой
// фона, поэтому обновляется без мерцания и без полной очистки экрана.
void answerText(const std::vector<std::string>& lines, size_t from, int color);

// Индикатор страниц внизу справа.
// Счётчик страниц «x/y» в верхнем левом углу. Показывается всегда, в том
// числе во время стрима, когда y ещё растёт.
void pageIndicator(size_t page, size_t total);

// Панель с коротким уведомлением по центру (например, о сбросе истории).
void notice(const char* text);

// Индикатор удержания кнопки: тонкая полоска внизу экрана.
void holdProgress(int percent);

// Метка справа от пункта: что с ним на стике.
//   Installed — стоит и обновлять нечего (зелёная точка)
//   Update    — стоит, но в репозитории новее (оранжевая точка в кольце)
//   None      — не установлено (ничего)
enum class Mark { None, Installed, Update };

struct ListItem {
  std::string text;
  std::string value;  // правое поле: текущее значение настройки (может пустовать)
  Mark mark = Mark::None;

  ListItem() = default;
  ListItem(const std::string& t, Mark m = Mark::None) : text(t), mark(m) {}
  ListItem(const std::string& t, const std::string& v) : text(t), value(v) {}
};

// Список с заголовком: пункты, выделение и прокрутка. Используется
// меню настроек и панелью плагинов.
void list(const char* title, const std::vector<ListItem>& items, size_t index);

// Погасить/вернуть подсветку (пиксели не трогаем — картинка сохраняется).
void screenOff();
void screenOn();

}  // namespace ui
