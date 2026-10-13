#include "pure_test.h"
#include "hold_button.h"

using holdbtn::Hold;

// Полный цикл: нажали, подержали, порог — Fired, отпустили — уже не Tap.
static void test_fire() {
  Hold h(3000);
  CHECK(h.poll(100, true, true, false) == Hold::Event::None);
  CHECK(h.holding());

  CHECK(h.poll(1000, true, false, false) == Hold::Event::Progress);
  CHECK(h.percent() == 30);

  CHECK(h.poll(2999, true, false, false) == Hold::Event::Progress);
  CHECK(h.percent() == 96);  // отсчёт идёт от нажатия (100 мс), а не от нуля

  CHECK(h.poll(3100, true, false, false) == Hold::Event::Fired);
  CHECK(!h.holding());
  CHECK(h.percent() == 0);

  // Отпускание после срабатывания тапом не считается.
  CHECK(h.poll(3200, false, false, true) == Hold::Event::None);
}

// Отпустили раньше порога — это тап, и полоска гаснет.
static void test_tap() {
  Hold h(3000);
  h.poll(0, true, true, false);
  CHECK(h.poll(500, true, false, false) == Hold::Event::Progress);
  CHECK(h.percent() == 16);

  CHECK(h.poll(600, false, false, true) == Hold::Event::Tap);
  CHECK(h.percent() == 0);
  CHECK(!h.holding());
}

// Просто отпускание, без нажатия в этом же объекте, — ничего.
static void test_release_without_press() {
  Hold h(3000);
  CHECK(h.poll(10, false, false, true) == Hold::Event::None);
}

// После тапа объект готов к следующему удержанию.
static void test_reuse() {
  Hold h(1000);
  h.poll(0, true, true, false);
  CHECK(h.poll(100, false, false, true) == Hold::Event::Tap);

  CHECK(h.poll(200, true, true, false) == Hold::Event::None);
  CHECK(h.poll(1200, true, false, false) == Hold::Event::Fired);
  CHECK(h.poll(1300, false, false, true) == Hold::Event::None);
}

// Ровно на пороге — уже сработало.
static void test_exact_threshold() {
  Hold h(3000);
  h.poll(0, true, true, false);
  CHECK(h.poll(3000, true, false, false) == Hold::Event::Fired);
}

// Нулевой порог не должен делить на ноль.
static void test_zero_threshold() {
  Hold h(0);
  h.poll(0, true, true, false);
  CHECK(h.poll(1, true, false, false) == Hold::Event::Fired);
}

// Удержание переживает переполнение millis() (через ~49 суток).
static void test_millis_wrap() {
  const uint32_t near = 0xFFFFFF00u;
  Hold h(1000);
  h.poll(near, true, true, false);
  CHECK(h.poll(near + 500, true, false, false) == Hold::Event::Progress);
  CHECK(h.poll(near + 1000, true, false, false) == Hold::Event::Fired);
}

int main() {
  test_fire();
  test_tap();
  test_release_without_press();
  test_reuse();
  test_exact_threshold();
  test_zero_threshold();
  test_millis_wrap();
  return puretest::summary("hold_button");
}
