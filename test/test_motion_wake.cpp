#include "pure_test.h"
#include "motion_wake.h"

// Мусорный отсчёт (сбой чтения) не будит: именно он держал экран включённым.
static void test_garbage_never_wakes() {
  motion::Wake w;
  for (int i = 0; i < 50; ++i) CHECK(!w.feed(0.12f));
  CHECK(!w.feed(0.0f));
  CHECK(!w.feed(0.3f));
}

// Покой не будит, сколько ни жди.
static void test_rest_never_wakes() {
  motion::Wake w;
  for (int i = 0; i < 50; ++i) CHECK(!w.feed(1.0f));
}

// Подъём: несколько отсчётов подряд — будим.
static void test_pickup_wakes_after_hits() {
  motion::Wake w;
  CHECK(!w.feed(1.5f));   // одного мало
  CHECK(!w.feed(1.5f));   // двух мало
  CHECK(w.feed(1.5f));    // три подряд — будим
}

// Движение вниз (меньше 1 g) тоже будит — порог симметричный.
static void test_downward_motion_wakes() {
  motion::Wake w;
  CHECK(!w.feed(0.6f));
  CHECK(!w.feed(0.6f));
  CHECK(w.feed(0.6f));
}

// Случайный одиночный толчок не будит: попадания должны идти подряд.
static void test_single_bump_does_not_wake() {
  motion::Wake w;
  CHECK(!w.feed(1.5f));
  CHECK(!w.feed(1.0f));  // вернулись в покой — счётчик сбрасывается
  CHECK(!w.feed(1.5f));
  CHECK(!w.feed(1.5f));
  CHECK(w.feed(1.5f));   // а вот три подряд после сброса — будим
}

// Мусорный отсчёт между настоящими движениями сбрасывает счётчик: серия
// рвётся, и экран не проснётся от одного сбоя рядом с толчком.
static void test_garbage_resets_streak() {
  motion::Wake w;
  CHECK(!w.feed(1.5f));
  CHECK(!w.feed(1.5f));
  CHECK(!w.feed(0.05f));  // сбой чтения
  CHECK(!w.feed(1.5f));   // счёт пошёл заново
  CHECK(!w.feed(1.5f));
  CHECK(w.feed(1.5f));
}

// После пробуждения счётчик чист: следующее движение снова требует серии.
static void test_counter_resets_after_wake() {
  motion::Wake w;
  CHECK(!w.feed(1.5f));
  CHECK(!w.feed(1.5f));
  CHECK(w.feed(1.5f));
  CHECK(!w.feed(1.5f));   // одного мало
  CHECK(!w.feed(1.5f));
  CHECK(w.feed(1.5f));
}

// Настройки порогов: чем выше порог, тем труднее разбудить.
static void test_custom_threshold() {
  motion::Wake w(2.0f, 2, 0.5f);
  CHECK(!w.feed(1.5f));   // ниже порога 2.0
  CHECK(!w.feed(2.5f));   // одного мало, нужно два
  CHECK(w.feed(2.5f));
}

int main() {
  test_garbage_never_wakes();
  test_rest_never_wakes();
  test_pickup_wakes_after_hits();
  test_downward_motion_wakes();
  test_single_bump_does_not_wake();
  test_garbage_resets_streak();
  test_counter_resets_after_wake();
  test_custom_threshold();
  return puretest::summary("motion_wake");
}
