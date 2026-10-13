// Удержание кнопки с полоской прогресса снизу экрана.
//
// Общая обвязка для всех мест, где «зажми, чтобы сделать»: сброс истории на
// главном экране и выход из меню настроек. Сам отсчёт — в pure/hold_button.h
// (чистая логика с тестами), здесь только кнопка M5 и рисование полоски.
#pragma once

#include <cstdint>

#include <M5Unified.h>

#include "pure/hold_button.h"

namespace hold {

// Те же события, что у чистой логики: Progress можно игнорировать —
// полоску Watcher рисует сам.
enum class Event { None, Progress, Fired, Tap };

class Watcher {
 public:
  Watcher(const m5::Button_Class& button, uint32_t thresholdMs)
      : button_(button), logic_(thresholdMs) {}

  // Вызывать каждый цикл. Полоску рисует не чаще, чем нужно глазу.
  Event poll();

  // Погасить полоску — например, когда экран сменился.
  void clear();

 private:
  const m5::Button_Class& button_;
  holdbtn::Hold logic_;
  uint32_t lastDraw_ = 0;
};

}  // namespace hold
