#include "pure_test.h"
#include "cyrillic.h"

static void test_count_codepoints_ascii() {
  CHECK_EQ(cyr::countCodepoints("hello"), (size_t)5);
  CHECK_EQ(cyr::countCodepoints(""), (size_t)0);
}

static void test_count_codepoints_cyrillic() {
  // «Привет» — 6 букв, каждая по 2 байта в UTF-8
  CHECK_EQ(cyr::countCodepoints("Привет"), (size_t)6);
  CHECK_EQ(cyr::countCodepoints("П"), (size_t)1);
}

static void test_cyrillic_bytes_are_two() {
  const char* s = "Привет";
  CHECK_EQ(cyr::nextCodepoint(s, 0), (size_t)2);
}

static void test_decode_codepoint() {
  CHECK_EQ(cyr::codepointAt("A", 0), (uint32_t)'A');
  // Б — U+0411 (кириллическая заглавная Б)
  CHECK_EQ(cyr::codepointAt("\xd0\x91", 0), (uint32_t)0x0411);
  // я — U+044F
  CHECK_EQ(cyr::codepointAt("\xd1\x8f", 0), (uint32_t)0x044F);
}

static void test_next_steps_through_mixed() {
  const char* s = "AБB";  // 1 + 2 + 1 байт
  size_t i = 0;
  CHECK_EQ((int)cyr::codepointAt(s, i), (int)'A');
  i += cyr::nextCodepoint(s, i);
  CHECK_EQ((int)cyr::codepointAt(s, i), 0x0411);
  i += cyr::nextCodepoint(s, i);
  CHECK_EQ((int)cyr::codepointAt(s, i), (int)'B');
}

static void test_text_width() {
  // Ширина считается в пикселях: ASCII — половина от cell,
  // кириллица — полная ширина cell. Формула: ceil для половины.
  cyr::setCellWidth(8);
  CHECK_EQ(cyr::textWidth("AB"), 8);        // 2 * 4
  CHECK_EQ(cyr::textWidth("АБ"), 16);       // 2 * 8
  CHECK_EQ(cyr::textWidth("AБ"), 12);       // 4 + 8
  CHECK_EQ(cyr::textWidth(""), 0);
}

int main() {
  test_count_codepoints_ascii();
  test_count_codepoints_cyrillic();
  test_cyrillic_bytes_are_two();
  test_decode_codepoint();
  test_next_steps_through_mixed();
  test_text_width();
  return puretest::summary("cyrillic");
}
