#include "pure_test.h"
#include "panel_life.h"

// Ускоренная политика: простоя 10 «тиков», потолок 100.
static panellife::Policy fast() { return panellife::Policy(10, 100); }

// Пока запросы идут, панель жива.
static void test_requests_keep_alive() {
  panellife::Policy p = fast();
  uint32_t last = 0;
  for (uint32_t t = 0; t <= 90; t += 5) {
    CHECK(!p.shouldClose(t, 0, last));
    last = t;  // каждый запрос продлевает
  }
}

// Запросы прекратились — панель закрывается по простою.
static void test_idle_closes() {
  panellife::Policy p = fast();
  CHECK(!p.shouldClose(5, 0, 0));   // 5 < 10
  CHECK(p.shouldClose(10, 0, 0));   // ровно порог — закрываем
  CHECK(p.shouldClose(40, 0, 0));
}

// Даже с запросами есть потолок: забытая вкладка не держит панель вечно.
static void test_max_lifetime_closes() {
  panellife::Policy p = fast();
  CHECK(!p.shouldClose(99, 0, 99));   // запрос только что, но 99 < 100
  CHECK(p.shouldClose(100, 0, 100));  // потолок с момента открытия
}

// Потолок считается от открытия, а не от последнего запроса: иначе поток
// запросов отодвигал бы его бесконечно.
static void test_max_counts_from_open() {
  panellife::Policy p = fast();
  CHECK(!p.maxExpired(90, 0));
  CHECK(p.maxExpired(100, 0));
  CHECK(p.maxExpired(150, 0));
}

// Переполнение счётчика миллис работает: 32-битное время заворачивается
// примерно раз в 49 суток, и сравнение не должно ломаться.
static void test_millis_wraparound() {
  panellife::Policy p = fast();
  const uint32_t opened = 0xFFFFFFF0u;  // почти переполнение
  CHECK(!p.idleExpired(opened + 5, opened));
  CHECK(p.idleExpired(opened + 10, opened));
  CHECK(!p.maxExpired(opened + 50, opened));
  CHECK(p.maxExpired(opened + 100, opened));
}

// Свои настройки: длинный простой и короткий потолок.
static void test_custom_policy() {
  panellife::Policy p(1000, 5000);
  CHECK(!p.shouldClose(500, 0, 0));
  CHECK(p.shouldClose(1000, 0, 0));     // простой
  CHECK(p.shouldClose(5000, 0, 4999));  // потолок
}

int main() {
  test_requests_keep_alive();
  test_idle_closes();
  test_max_lifetime_closes();
  test_max_counts_from_open();
  test_millis_wraparound();
  test_custom_policy();
  return puretest::summary("panel_life");
}
