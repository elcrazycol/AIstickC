#include "pure_test.h"
#include "elapsed.h"

static const char* fmt(uint32_t ms) {
  static char buf[16];
  elapsed::format(ms, buf, sizeof(buf));
  return buf;
}

// До минуты — просто секунды.
static void test_seconds() {
  CHECK_STR_EQ(fmt(0), "0 с");
  CHECK_STR_EQ(fmt(999), "0 с");
  CHECK_STR_EQ(fmt(1000), "1 с");
  CHECK_STR_EQ(fmt(12345), "12 с");
  CHECK_STR_EQ(fmt(59999), "59 с");
}

// С минуты — как на часах, чтобы «120 с» не читалось.
static void test_minutes() {
  CHECK_STR_EQ(fmt(60000), "1:00");
  CHECK_STR_EQ(fmt(65000), "1:05");
  CHECK_STR_EQ(fmt(754000), "12:34");
}

// Буфер не переполняется даже на большом времени.
static void test_no_overflow() {
  char small[8];
  elapsed::format(3600000u, small, sizeof(small));
  CHECK(small[sizeof(small) - 1] == 0);
}

int main() {
  test_seconds();
  test_minutes();
  test_no_overflow();
  return puretest::summary("elapsed");
}
