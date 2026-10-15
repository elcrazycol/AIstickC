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
constexpr int kPageBase  = 8;    // центр строки состояния для счётчика страниц

// Полоса волн внизу экрана записи.
constexpr int kWaveBars = 58;   // 58*(3+1)-1 = 231 px
constexpr int kWaveH    = 30;

// Инициализация: шрифт, мьютекс дисплея и задача анимации точек.
void begin();

// Полоса состояния: Wi-Fi слева, имя модели справа.
void statusBar();

// Экран веб-панели: выключатель. Выключена — красная надпись и подсказка;
// включена — QR с адресом, код доступа и состояние связи.
void panel(bool on, bool used, const char* url, const char* ip,
           const char* code);

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

// Строка ответа. code — строка из блока кода: её рисуем мелким шрифтом на
// тёмной подложке с зелёной полоской слева, чтобы отличалась от текста.
struct Line {
  std::string text;
  bool code = false;

  Line() = default;
  Line(const std::string& t, bool c = false) : text(t), code(c) {}
};

// Текст ответа: окно из kBodyLines строк, начиная с from. Рисуется с заливкой
// фона, поэтому обновляется без мерцания и без полной очистки экрана.
void answerText(const std::vector<Line>& lines, size_t from, int color);

// Строки [from, to) страницы, верхняя строка которой — pageFirst. Нужно во
// время стрима: там меняется только последняя строка, и перерисовывать всю
// страницу двадцать раз в секунду незачем.
// noteLine — индекс строки-пометки инструмента (мелким серым шрифтом);
// (size_t)-1, если пометки нет.
void answerLines(const std::vector<Line>& lines, size_t pageFirst, size_t from,
                 size_t to, int color, size_t noteLine = (size_t)-1);

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

// Экран ожидания инструмента: короткая надпись, что делает плагин, плюс
// бегущие точки строкой ниже.
void toolWait(const char* label);

// Список с заголовком: пункты, выделение и прокрутка. Используется
// меню настроек и панелью плагинов.
void list(const char* title, const std::vector<ListItem>& items, size_t index);

// Погасить/вернуть подсветку (пиксели не трогаем — картинка сохраняется).
void screenOff();
void screenOn();

}  // namespace ui
