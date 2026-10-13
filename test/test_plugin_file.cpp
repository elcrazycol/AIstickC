#include "pure_test.h"
#include "plugin_file.h"

#include <string>
#include <vector>

static const char* kSample =
    "# комментарий\n"
    "\n"
    "name = set_lamp\n"
    "description = Лампа на GPIO26 # хвостовой комментарий\n"
    "kind = gpio\n"
    "pin = 26\n"
    "\n"
    "name = show_text\n"
    "description = Показать текст\n"
    "kind = screen\n"
    "answer = true\n"
    "\n"
    "name = http_get\n"
    "description = Вебхук\n"
    "kind = http\n"
    "url = https://example.com/hook\n";

static void test_three_plugins() {
  std::vector<plugfile::Def> defs;
  CHECK_EQ(plugfile::parse(kSample, defs), (size_t)3);
  CHECK_EQ(defs.size(), (size_t)3);

  CHECK_STR_EQ(defs[0].name, "set_lamp");
  CHECK_STR_EQ(defs[0].description, "Лампа на GPIO26");
  CHECK_STR_EQ(defs[0].kind, "gpio");
  CHECK_EQ(defs[0].pin, 26);
  CHECK(!defs[0].answerAfter);

  CHECK_STR_EQ(defs[1].kind, "screen");
  CHECK(defs[1].answerAfter);

  CHECK_STR_EQ(defs[2].kind, "http");
  CHECK_STR_EQ(defs[2].url, "https://example.com/hook");
}

static void test_last_block_without_blank_line() {
  std::vector<plugfile::Def> defs;
  const char* text =
      "name = a\nkind = screen\ndescription = x\n\n"
      "name = b\nkind = gpio\npin = 5\n";
  CHECK_EQ(plugfile::parse(text, defs), (size_t)2);
  CHECK_STR_EQ(defs[1].name, "b");
  CHECK_EQ(defs[1].pin, 5);
}

static void test_block_without_name_skipped() {
  std::vector<plugfile::Def> defs;
  const char* text = "kind = gpio\npin = 26\n\nname = ok\nkind = screen\n";
  CHECK_EQ(plugfile::parse(text, defs), (size_t)1);
  CHECK_STR_EQ(defs[0].name, "ok");
}

static void test_unknown_kind_skipped() {
  std::vector<plugfile::Def> defs;
  const char* text =
      "name = a\nkind = teleport\n\nname = b\nkind = screen\n";
  CHECK_EQ(plugfile::parse(text, defs), (size_t)1);
  CHECK_STR_EQ(defs[0].name, "b");
}

static void test_crlf_and_spaces() {
  std::vector<plugfile::Def> defs;
  const char* text = "name   =   spaced\r\nkind = screen\r\n\r\n";
  CHECK_EQ(plugfile::parse(text, defs), (size_t)1);
  CHECK_STR_EQ(defs[0].name, "spaced");
  CHECK_STR_EQ(defs[0].kind, "screen");
}

static void test_empty_and_null() {
  std::vector<plugfile::Def> defs;
  CHECK_EQ(plugfile::parse("", defs), (size_t)0);
  CHECK_EQ(plugfile::parse("\n\n\n", defs), (size_t)0);
  CHECK_EQ(plugfile::parse(nullptr, defs), (size_t)0);
}

static void test_no_equals_line_ignored() {
  std::vector<plugfile::Def> defs;
  const char* text = "мусор без равно\nname = a\nkind = http\n";
  CHECK_EQ(plugfile::parse(text, defs), (size_t)1);
}

static void test_answer_variants() {
  std::vector<plugfile::Def> defs;
  plugfile::parse("name = a\nkind = screen\nanswer = 1\n\n"
                  "name = b\nkind = screen\nanswer = false\n", defs);
  CHECK(defs[0].answerAfter);
  CHECK(!defs[1].answerAfter);
}

static void test_schema_by_kind() {
  CHECK(std::string(plugfile::schemaFor("gpio")).find("\"state\"") !=
        std::string::npos);
  CHECK(std::string(plugfile::schemaFor("screen")).find("\"text\"") !=
        std::string::npos);
  CHECK(std::string(plugfile::schemaFor("http")).find("\"query\"") !=
        std::string::npos);
  // Неизвестный вид — как gpio (безопасный минимум).
  CHECK(std::string(plugfile::schemaFor("??")).find("\"state\"") !=
        std::string::npos);
}

int main() {
  test_three_plugins();
  test_last_block_without_blank_line();
  test_block_without_name_skipped();
  test_unknown_kind_skipped();
  test_crlf_and_spaces();
  test_empty_and_null();
  test_no_equals_line_ignored();
  test_answer_variants();
  test_schema_by_kind();
  return puretest::summary("plugin_file");
}
