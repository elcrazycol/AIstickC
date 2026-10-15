#include "pure_test.h"
#include "json_write.h"

#include <string>

static std::string q(const char* s) {
  std::string out;
  jsonw::quoted(std::string(s), out);
  return out;
}

// Обычный текст остаётся собой, но в кавычках.
static void test_plain() {
  CHECK_STR_EQ(q("привет"), "\"привет\"");
  CHECK_STR_EQ(q(""), "\"\"");
}

// Кавычка внутри ответа — главный случай: именно она ломала бы разбор.
static void test_quotes() {
  CHECK_STR_EQ(q("он сказал \"да\""), "\"он сказал \\\"да\\\"\"");
}

// Обратный слэш и переводы строк.
static void test_slashes_and_newlines() {
  CHECK_STR_EQ(q("a\\b"), "\"a\\\\b\"");
  CHECK_STR_EQ(q("первая\nвторая"), "\"первая\\nвторая\"");
  CHECK_STR_EQ(q("таб\tтут"), "\"таб\\tтут\"");
}

// Кириллица не экранируется: это обычный UTF-8, браузер её поймёт.
static void test_cyrillic_untouched() {
  CHECK_STR_EQ(q("ёжик"), "\"ёжик\"");
}

// Управляющие символы уходят в \u00XX.
static void test_control_chars() {
  CHECK_STR_EQ(q("a\x01z"), "\"a\\u0001z\"");
}

// Реплика целиком: и пользователь, и ответ.
static void test_message() {
  std::string out;
  jsonw::message(true, "сколько время", out);
  CHECK_STR_EQ(out, "{\"u\":true,\"t\":\"сколько время\"}");

  out.clear();
  jsonw::message(false, "полдень \"ровно\"", out);
  CHECK_STR_EQ(out, "{\"u\":false,\"t\":\"полдень \\\"ровно\\\"\"}");
}

// Склейка массива из нескольких реплик — как это делает панель.
static void test_array() {
  std::string out = "[";
  jsonw::message(true, "вопрос", out);
  out += ",";
  jsonw::message(false, "ответ", out);
  out += "]";
  CHECK_STR_EQ(out, "[{\"u\":true,\"t\":\"вопрос\"},{\"u\":false,\"t\":\"ответ\"}]");
}

int main() {
  test_plain();
  test_quotes();
  test_slashes_and_newlines();
  test_cyrillic_untouched();
  test_control_chars();
  test_message();
  test_array();
  return puretest::summary("json_write");
}
