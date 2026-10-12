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

// --- Разбор по вложенному пути (ответ OpenCode Go) ---

static void test_path_nested_content() {
  std::string body =
      R"({"id":"x","choices":[{"index":0,"message":{"role":"assistant","content":"привет"}}]})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
  CHECK_STR_EQ(out, "привет");
}

static void test_path_reasoning_content_beside() {
  // reasoning_content идёт перед content — его надо перешагнуть.
  std::string body =
      R"({"choices":[{"message":{"reasoning_content":"долго думал","content":"ответ"}}]})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
  CHECK_STR_EQ(out, "ответ");
}

static void test_path_second_element() {
  std::string body =
      R"({"choices":[{"message":{"content":"первый"}},{"message":{"content":"второй"}}]})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "choices/[1]/message/content", out));
  CHECK_STR_EQ(out, "второй");
}

static void test_path_error_message() {
  std::string body =
      R"({"error":{"message":"Request is missing x-opencode-session","type":"invalid_request_error"}})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "error/message", out));
  CHECK_STR_EQ(out, "Request is missing x-opencode-session");
}

static void test_path_top_level_key() {
  // Путь из одного токена работает как плоский ключ.
  std::string body = R"({"text":"плоско"})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "плоско");
}

static void test_path_escapes_inside_content() {
  std::string body =
      R"({"choices":[{"message":{"content":"он сказал \"да\"\nвсё"}}]})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
  CHECK_STR_EQ(out, "он сказал \"да\"\nвсё");
}

static void test_path_empty_choices_fails() {
  std::string body = R"({"choices":[]})";
  std::string out;
  CHECK(!jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
}

static void test_path_missing_content_fails() {
  std::string body = R"({"choices":[{"message":{"role":"assistant"}}]})";
  std::string out;
  CHECK(!jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
}

static void test_path_wrong_type_fails() {
  // choices — не массив.
  std::string body = R"({"choices":"нет"})";
  std::string out;
  CHECK(!jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
}

static void test_path_truncated_fails() {
  std::string body = R"({"choices":[{"message":{"content":"обрыв)";
  std::string out;
  CHECK(!jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
}

static void test_path_skips_key_word_in_value() {
  // Слово message встречается в значении model — его не спутать с ключом.
  std::string body =
      R"({"model":"message-model","choices":[{"message":{"content":"верно"}}]})";
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "choices/[0]/message/content", out));
  CHECK_STR_EQ(out, "верно");
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
  test_path_nested_content();
  test_path_reasoning_content_beside();
  test_path_second_element();
  test_path_error_message();
  test_path_top_level_key();
  test_path_escapes_inside_content();
  test_path_empty_choices_fails();
  test_path_missing_content_fails();
  test_path_wrong_type_fails();
  test_path_truncated_fails();
  test_path_skips_key_word_in_value();
  return puretest::summary("json");
}
