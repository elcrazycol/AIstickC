#include "pure_test.h"
#include "text_wrap.h"

#include <string>
#include <vector>

// Ширина: один пиксель на любой символ.
static int one(const char*, size_t, void*) { return 1; }

// Кириллица (2 байта) — 2 пикселя, остальное — 1.
static int cyrTwo(const char*, size_t len, void*) {
  return (len == 2) ? 2 : 1;
}

static size_t cps(const std::string& s) {
  size_t n = 0, i = 0;
  while (i < s.size()) {
    const size_t cl = textwrap::charLen((unsigned char)s[i]);
    i += cl == 0 ? 1 : cl;
    ++n;
  }
  return n;
}

// Строка — цельная последовательность UTF-8, без разреза посреди символа.
static bool intact(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    const size_t cl = textwrap::charLen((unsigned char)s[i]);
    if (cl == 0 || i + cl > s.size()) return false;
    for (size_t k = 1; k < cl; ++k) {
      if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
    }
    i += cl;
  }
  return true;
}

static std::vector<std::string> run(const char* text, int w, textwrap::MeasureFn m) {
  std::vector<std::string> out;
  textwrap::wrap(text, w, m, nullptr, out);
  return out;
}

static void test_simple_word_wrap() {
  auto l = run("aaa bbb ccc", 7, one);
  CHECK_EQ(l.size(), (size_t)2);
  CHECK_STR_EQ(l[0], "aaa bbb");
  CHECK_STR_EQ(l[1], "ccc");
}

static void test_long_word_breaks() {
  auto l = run("aaaaaaaa", 3, one);
  CHECK_EQ(l.size(), (size_t)3);
  CHECK_STR_EQ(l[0], "aaa");
  CHECK_STR_EQ(l[1], "aaa");
  CHECK_STR_EQ(l[2], "aa");
}

static void test_word_exactly_fits() {
  auto l = run("aaa bbb", 7, one);
  CHECK_EQ(l.size(), (size_t)1);
  CHECK_STR_EQ(l[0], "aaa bbb");
}

static void test_cyrillic_not_split() {
  auto l = run("абвгде", 3, one);
  CHECK_EQ(l.size(), (size_t)2);
  CHECK_STR_EQ(l[0], "абв");
  CHECK_STR_EQ(l[1], "где");
  CHECK(intact(l[0]));
  CHECK(intact(l[1]));
  CHECK_EQ(l[0].size(), (size_t)6);  // 3 буквы по 2 байта
}

static void test_forced_break_on_codepoint() {
  // Ширина 2 режет «абв» ровно между буквами, а не внутри.
  auto l = run("абв", 2, one);
  CHECK_EQ(l.size(), (size_t)2);
  CHECK_STR_EQ(l[0], "аб");
  CHECK_STR_EQ(l[1], "в");
  CHECK(intact(l[0]));
  CHECK(intact(l[1]));
}

static void test_cyrillic_width_matters() {
  // "аб"=4, пробел=1, "в"=2 → 7 > 5, значит перенос.
  auto l = run("аб в", 5, cyrTwo);
  CHECK_EQ(l.size(), (size_t)2);
  CHECK_STR_EQ(l[0], "аб");
  CHECK_STR_EQ(l[1], "в");
}

static void test_hard_newline() {
  auto l = run("ab\ncd", 100, one);
  CHECK_EQ(l.size(), (size_t)2);
  CHECK_STR_EQ(l[0], "ab");
  CHECK_STR_EQ(l[1], "cd");
}

static void test_spaces_collapse() {
  auto l = run("a   b", 100, one);
  CHECK_EQ(l.size(), (size_t)1);
  CHECK_STR_EQ(l[0], "a b");
}

static void test_empty_text_one_line() {
  auto l = run("", 10, one);
  CHECK_EQ(l.size(), (size_t)1);
  CHECK_STR_EQ(l[0], "");
}

static void test_null_text_one_line() {
  std::vector<std::string> l;
  textwrap::wrap(nullptr, 10, one, nullptr, l);
  CHECK_EQ(l.size(), (size_t)1);
  CHECK_STR_EQ(l[0], "");
}

static void test_every_line_within_width() {
  const std::string text =
      "Привет! У меня нет имени — я просто твой карманный ассистент. "
      "Можешь называть меня как удобно, спасибо.";
  auto l = run(text.c_str(), 10, one);
  CHECK(l.size() > 3);
  for (const auto& s : l) {
    CHECK(cps(s) <= 10);
    CHECK(intact(s));
  }
}

static void test_no_empty_lines_from_wrapping() {
  auto l = run("word word word word", 9, one);
  for (const auto& s : l) CHECK(!s.empty());
}

static void test_pagination_counts() {
  CHECK_EQ(textwrap::pageCount(0, 3), (size_t)1);
  CHECK_EQ(textwrap::pageCount(3, 3), (size_t)1);
  CHECK_EQ(textwrap::pageCount(4, 3), (size_t)2);
  CHECK_EQ(textwrap::pageCount(10, 3), (size_t)4);
  CHECK_EQ(textwrap::pageCount(5, 0), (size_t)1);  // защита от нуля
}

static void test_pagination_ranges() {
  size_t b = 0, e = 0;
  textwrap::pageRange(0, 10, 3, b, e);
  CHECK_EQ(b, (size_t)0);
  CHECK_EQ(e, (size_t)3);
  textwrap::pageRange(3, 10, 3, b, e);
  CHECK_EQ(b, (size_t)9);
  CHECK_EQ(e, (size_t)10);
  // Страница за верхней границей зажимается к последней.
  textwrap::pageRange(99, 10, 3, b, e);
  CHECK_EQ(b, (size_t)9);
  CHECK_EQ(e, (size_t)10);
  // Пустой текст.
  textwrap::pageRange(0, 0, 3, b, e);
  CHECK_EQ(b, (size_t)0);
  CHECK_EQ(e, (size_t)0);
}

int main() {
  test_simple_word_wrap();
  test_long_word_breaks();
  test_word_exactly_fits();
  test_cyrillic_not_split();
  test_forced_break_on_codepoint();
  test_cyrillic_width_matters();
  test_hard_newline();
  test_spaces_collapse();
  test_empty_text_one_line();
  test_null_text_one_line();
  test_every_line_within_width();
  test_no_empty_lines_from_wrapping();
  test_pagination_counts();
  test_pagination_ranges();
  return puretest::summary("text_wrap");
}
