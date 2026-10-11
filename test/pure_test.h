// Крошечная тестовая обвязка. Заменяет Unity, который требует сети.
#pragma once
#include <cstdio>
#include <cmath>
#include <string>

namespace puretest {

inline int& failures() { static int n = 0; return n; }
inline int& checks()   { static int n = 0; return n; }

inline void report(const char* file, int line, const char* expr, bool ok) {
  ++checks();
  if (!ok) {
    ++failures();
    std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
  }
}

inline int summary(const char* suite) {
  if (failures() == 0) {
    std::printf("PASS %s (%d проверок)\n", suite, checks());
    return 0;
  }
  std::printf("FAIL %s (%d из %d провалено)\n", suite, failures(), checks());
  return 1;
}

}  // namespace puretest

#define CHECK(cond) ::puretest::report(__FILE__, __LINE__, #cond, (cond))

#define CHECK_EQ(a, b)                                                      \
  do {                                                                      \
    auto _a = (a);                                                          \
    auto _b = (b);                                                          \
    ::puretest::report(__FILE__, __LINE__, #a " == " #b, _a == _b);         \
  } while (0)

#define CHECK_STR_EQ(a, b)                                                  \
  do {                                                                      \
    std::string _a((a));                                                    \
    std::string _b((b));                                                    \
    ::puretest::report(__FILE__, __LINE__, #a " == " #b, _a == _b);         \
  } while (0)
