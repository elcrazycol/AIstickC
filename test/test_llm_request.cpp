#include "pure_test.h"
#include "json_extract.h"
#include "llm_request.h"

#include <string>

static void test_body_has_model_and_roles() {
  std::string body = llmreq::build("deepseek-v4.1-flash", "система", "привет", 150);
  CHECK(body.find("\"model\":\"deepseek-v4.1-flash\"") != std::string::npos);
  CHECK(body.find("\"role\":\"system\"") != std::string::npos);
  CHECK(body.find("\"role\":\"user\"") != std::string::npos);
  CHECK(body.find("\"max_tokens\":150") != std::string::npos);
  CHECK_EQ(body.front(), '{');
  CHECK_EQ(body.back(), '}');
}

// Главная проверка: собранное тело — валидный JSON, из которого тот же
// парсер достаёт исходный пользовательский текст.
static std::string userRoundTrip(const std::string& user) {
  std::string body = llmreq::build("m", "s", user.c_str(), 10);
  std::string out;
  const bool ok = jsonx::extractPath(
      body.c_str(), "messages/[1]/content", out);
  if (!ok) return "<НЕ РАЗОБРАЛОСЬ>";
  return out;
}

static void test_round_trip_plain() {
  CHECK_STR_EQ(userRoundTrip("привет мир"), "привет мир");
}

static void test_round_trip_quotes() {
  CHECK_STR_EQ(userRoundTrip("он сказал \"да\""), "он сказал \"да\"");
}

static void test_round_trip_backslashes() {
  CHECK_STR_EQ(userRoundTrip("путь C:\\tmp\\a"), "путь C:\\tmp\\a");
}

static void test_round_trip_newlines_and_tabs() {
  CHECK_STR_EQ(userRoundTrip("строка1\nстрока2\tконец"), "строка1\nстрока2\tконец");
}

static void test_round_trip_cyrillic_kept_verbatim() {
  // Кириллица не должна превратиться в \uXXXX.
  const std::string body = llmreq::build("m", "s", "Привет, ёж!", 10);
  CHECK(body.find("Привет, ёж!") != std::string::npos);
  CHECK_STR_EQ(userRoundTrip("Привет, ёж!"), "Привет, ёж!");
}

static void test_control_char_becomes_unicode_escape() {
  std::string user = "a";
  user.push_back((char)0x01);  // управляющий символ
  user += "b";
  const std::string body = llmreq::build("m", "s", user.c_str(), 10);
  CHECK(body.find("\\u0001") != std::string::npos);
  // И обратно он же — тот же байт.
  const std::string back = userRoundTrip(user);
  CHECK_EQ(back.size(), (size_t)3);
  CHECK_EQ((unsigned char)back[1], 0x01);
}

static void test_system_prompt_round_trip() {
  std::string sys = "Ты ассистент. Отвечай \"кратко\".";
  std::string body = llmreq::build("m", sys.c_str(), "u", 5);
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "messages/[0]/content", out));
  CHECK_STR_EQ(out, sys);
}

static void test_empty_user_is_valid() {
  std::string body = llmreq::build("m", "s", "", 5);
  std::string out;
  CHECK(jsonx::extractPath(body.c_str(), "messages/[1]/content", out));
  CHECK_STR_EQ(out, "");
}

static void test_escaped_len_matches_write() {
  const char* samples[] = {
      "",
      "простой",
      "кавычка \" и слэш \\",
      "таб\tновлайн\n",
  };
  for (const char* s : samples) {
    const size_t n = llmreq::escapedLen(s);
    std::string buf(n ? n : 1, '\0');  // непустой буфер даже при n == 0
    const size_t written = llmreq::writeEscaped(&buf[0], s);
    CHECK_EQ(written, n);
    // Управляющих символов в выводе быть не должно.
    for (size_t k = 0; k < n; ++k) {
      const unsigned char u = (unsigned char)buf[k];
      CHECK(u >= 0x20);
    }
  }
}

int main() {
  test_body_has_model_and_roles();
  test_round_trip_plain();
  test_round_trip_quotes();
  test_round_trip_backslashes();
  test_round_trip_newlines_and_tabs();
  test_round_trip_cyrillic_kept_verbatim();
  test_control_char_becomes_unicode_escape();
  test_system_prompt_round_trip();
  test_empty_user_is_valid();
  test_escaped_len_matches_write();
  return puretest::summary("llm_request");
}
