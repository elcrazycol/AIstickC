#include "pure_test.h"
#include "sleep_options.h"

// Ступени: от двух минут до «никогда».
static void test_steps() {
  CHECK(sleepopt::kStepsCount == 5);
  CHECK(sleepopt::seconds(0) == 120);
  CHECK(sleepopt::seconds(4) == 0);  // никогда
  CHECK_STR_EQ(sleepopt::label(0), "2 мин");
  CHECK_STR_EQ(sleepopt::label(3), "1 час");
  CHECK_STR_EQ(sleepopt::label(4), "никогда");
}

// По умолчанию — час, как было до настройки.
static void test_default() {
  CHECK(sleepopt::kDefault == 3);
  CHECK(sleepopt::seconds(sleepopt::kDefault) == 3600);
  CHECK(sleepopt::ms(sleepopt::kDefault) == 3600000u);
}

// «Никогда» даёт ноль миллисекунд — по нему таймер сна не срабатывает.
static void test_never() {
  CHECK(sleepopt::ms(4) == 0);
}

// Перебор по кругу, включая возврат к началу.
static void test_next() {
  CHECK(sleepopt::next(0) == 1);
  CHECK(sleepopt::next(3) == 4);
  CHECK(sleepopt::next(4) == 0);
}

// Мусор из NVS превращается в значение по умолчанию.
static void test_invalid() {
  CHECK(!sleepopt::valid(5));
  CHECK(!sleepopt::valid(99));
  CHECK(sleepopt::clamp(99) == sleepopt::kDefault);
  CHECK_STR_EQ(sleepopt::label(99), "1 час");
}

// Ступени по возрастанию, кроме последней («никогда» = 0).
static void test_sorted() {
  for (size_t i = 1; i < sleepopt::kStepsCount - 1; ++i) {
    CHECK(sleepopt::seconds(i) > sleepopt::seconds(i - 1));
  }
}

int main() {
  test_steps();
  test_default();
  test_never();
  test_next();
  test_invalid();
  test_sorted();
  return puretest::summary("sleep_options");
}
