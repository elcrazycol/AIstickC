#include "pure_test.h"
#include "json_extract.h"

#include <string>

static void test_simple_text() {
  std::string body = R"({"text":"привет мир"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "привет мир");
}

static void test_text_with_spaces_around_colon() {
  std::string body = R"({ "text" : "пробелы" })";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "пробелы");
}

static void test_text_after_other_fields() {
  // У Groq поле text может идти не первым.
  std::string body =
      R"({"task":"transcribe","language":"ru","duration":5.0,"text":"готово"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "готово");
}

static void test_escaped_quotes() {
  std::string body = R"({"text":"он сказал \"да\""})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "он сказал \"да\"");
}

static void test_escaped_backslash_and_newline() {
  std::string body = R"({"text":"a\\b\nc"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "a\\b\nc");
}

static void test_missing_key_fails() {
  std::string body = R"({"error":"bad key"})";
  std::string out;
  CHECK(!jsonx::extractString(body.c_str(), "text", out));
}

static void test_truncated_json_fails() {
  // Ответ обрезан — не должно быть мусора на выходе.
  std::string body = R"({"text":"оборвано)";
  std::string out;
  CHECK(!jsonx::extractString(body.c_str(), "text", out));
}

static void test_empty_text_is_valid() {
  std::string body = R"({"text":""})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "");
}

static void test_key_inside_value_not_matched() {
  // Слово text встречается в значении другого поля — не путать с ключом.
  std::string body = R"({"prompt":"some text here","text":"верное"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "верное");
}

static void test_nested_object_skipped() {
  // Вложенный объект с полем text внутри — верхний уровень не путать.
  std::string body =
      R"({"segments":[{"text":"вложенный"}],"text":"верхний"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "верхний");
}

// Обратный слэш собираем кодом: в исходнике не должно быть
// последовательности из слэша и буквы u — редакторы и транспорты
// норовят превратить её в юникод-символ.
static const char BS = 0x5C;

static void test_unicode_escape_cyrillic() {
  // П=U+041F, р=U+0440, и=U+0438 — должно получиться «При».
  std::string body = std::string("{\"text\":\"") + BS + "u041F" + BS +
                     "u0440" + BS + "u0438\"}";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "При");
}

static void test_unicode_escape_ascii() {
  // U+0041 U+0042 U+0043 — должно получиться «ABC».
  std::string body = std::string("{\"text\":\"") + BS + "u0041" + BS +
                     "u0042" + BS + "u0043\"}";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "ABC");
}

static void test_unicode_escape_two_byte() {
  // U+044F — «я», двухбайтный UTF-8.
  std::string body = std::string("{\"text\":\"") + BS + "u044F\"}";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "я");
}

static void test_bad_unicode_escape_fails() {
  // Не hex после u — честное false, а не мусор.
  std::string body = std::string("{\"text\":\"") + BS + "uZZZZ\"}";
  std::string out;
  CHECK(!jsonx::extractString(body.c_str(), "text", out));
}

static void test_non_string_value_fails() {
  std::string body = R"({"text":42})";
  std::string out;
  CHECK(!jsonx::extractString(body.c_str(), "text", out));
}

int main() {
  test_simple_text();
  test_text_with_spaces_around_colon();
  test_text_after_other_fields();
  test_escaped_quotes();
  test_escaped_backslash_and_newline();
  test_missing_key_fails();
  test_truncated_json_fails();
  test_empty_text_is_valid();
  test_key_inside_value_not_matched();
  test_nested_object_skipped();
  test_unicode_escape_cyrillic();
  test_unicode_escape_ascii();
  test_unicode_escape_two_byte();
  test_bad_unicode_escape_fails();
  test_non_string_value_fails();
  return puretest::summary("json");
}
