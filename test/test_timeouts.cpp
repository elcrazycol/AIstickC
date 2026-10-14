#include "pure_test.h"
#include "timeouts.h"

// Каждая таблица: значения по возрастанию, «никогда» в конце (если есть).
static void test_tables() {
  const tmo::Table all[] = {tmo::screen(), tmo::wifi(), tmo::light(),
                            tmo::deep(), tmo::back(), tmo::reset()};
  for (const tmo::Table& t : all) {
    CHECK(t.count >= 2);
    CHECK(t.def < t.count);
    for (size_t i = 0; i < t.count; ++i) {
      CHECK(t.items[i].label != nullptr);
      CHECK(t.items[i].label[0] != 0);
    }
    // Значения растут. Ноль («никогда») может стоять только последним,
    // поэтому на нём проверку возрастания пропускаем.
    for (size_t i = 0; i + 1 < t.count; ++i) {
      CHECK(t.items[i].ms > 0);
      if (t.items[i + 1].ms != 0) CHECK(t.items[i].ms < t.items[i + 1].ms);
    }
  }
}

// Заводские значения — те, что были до настроек.
static void test_defaults() {
  CHECK(tmo::ms(tmo::screen(), tmo::screen().def) == 20000);
  CHECK(tmo::ms(tmo::wifi(), tmo::wifi().def) == 90000);
  CHECK(tmo::ms(tmo::light(), tmo::light().def) == 120000);
  CHECK(tmo::ms(tmo::deep(), tmo::deep().def) == 3600000);
  CHECK(tmo::ms(tmo::back(), tmo::back().def) == 850);
  CHECK(tmo::ms(tmo::reset(), tmo::reset().def) == 3000);
}

// «Никогда» даёт ноль — по нему таймер не срабатывает.
static void test_never() {
  const tmo::Table w = tmo::wifi();
  CHECK(tmo::ms(w, w.count - 1) == 0);
  const tmo::Table d = tmo::deep();
  CHECK(tmo::ms(d, d.count - 1) == 0);
}

// Перебор по кругу и защита от мусора из NVS.
static void test_next_and_clamp() {
  const tmo::Table b = tmo::back();
  CHECK(tmo::next(b, 0) == 1);
  CHECK(tmo::next(b, b.count - 1) == 0);
  CHECK(!tmo::valid(b, 99));
  CHECK(tmo::clamp(b, 99) == b.def);
  CHECK_STR_EQ(tmo::label(b, 99), "0.85 с");
}

// Подписи читаемые: без пустых строк и мусора.
static void test_labels() {
  CHECK_STR_EQ(tmo::label(tmo::screen(), 0), "5 с");
  CHECK_STR_EQ(tmo::label(tmo::screen(), 4), "1 мин");
  CHECK_STR_EQ(tmo::label(tmo::deep(), 3), "1 час");
  CHECK_STR_EQ(tmo::label(tmo::back(), 0), "0.5 с");
}

int main() {
  test_tables();
  test_defaults();
  test_never();
  test_next_and_clamp();
  test_labels();
  return puretest::summary("timeouts");
}
