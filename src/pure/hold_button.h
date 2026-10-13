// Удержание кнопки с отсчётом до порога.
//
// Чистая логика: время приходит снаружи, Arduino не нужен, поэтому поведение
// проверяется обычными тестами на Mac (test/test_hold_button.cpp).
//
// Жизненный цикл:
//   нажали → Progress (растёт полоска) → порог → Fired
//   отпустили раньше порога → Tap
#pragma once

#include <cstdint>

namespace holdbtn {

class Hold {
 public:
  enum class Event { None, Progress, Fired, Tap };

  explicit Hold(uint32_t thresholdMs)
      : threshold_(thresholdMs == 0 ? 1 : thresholdMs) {}

  // now — текущее время, мс. pressed — кнопка нажата прямо сейчас,
  // justPressed / justReleased — фронты за этот вызов.
  Event poll(uint32_t now, bool pressed, bool justPressed, bool justReleased);

  int percent() const { return percent_; }   // 0..100 — для полоски
  bool holding() const { return armed_; }    // идёт отсчёт

 private:
  uint32_t threshold_;
  uint32_t start_ = 0;
  bool armed_ = false;
  bool fired_ = false;
  int percent_ = 0;
};

inline Hold::Event Hold::poll(uint32_t now, bool pressed, bool justPressed,
                              bool justReleased) {
  if (justPressed) {
    armed_ = true;
    fired_ = false;
    start_ = now;
    percent_ = 0;
    return Event::None;
  }

  if (armed_ && pressed && !fired_) {
    const uint32_t held = now - start_;
    if (held >= threshold_) {
      armed_ = false;
      fired_ = true;  // на отпускании это уже не тап
      percent_ = 0;
      return Event::Fired;
    }
    percent_ = (int)((100ULL * held) / threshold_);
    return Event::Progress;
  }

  if (justReleased) {
    const bool wasHolding = armed_;
    armed_ = false;
    fired_ = false;
    percent_ = 0;
    return wasHolding ? Event::Tap : Event::None;
  }

  return Event::None;
}

}  // namespace holdbtn
