#include "pure_test.h"
#include "markdown.h"

#include <string>
#include <vector>

static std::vector<md::Line> parse(const char* s) {
  std::vector<md::Line> out;
  md::parse(s, out);
  return out;
}

// Маркеры выделения убираются, текст остаётся.
static void test_inline() {
  const auto l = parse("Это **жирный** и *косой* текст.");
  CHECK(l.size() == 1);
  CHECK_STR_EQ(l[0].text, "Это жирный и косой текст.");
  CHECK(!l[0].code);
}

// Код в обратных кавычках — просто текст, кавычки уходят.
static void test_inline_code() {
  const auto l = parse("Запускается словом `go` перед функцией.");
  CHECK_STR_EQ(l[0].text, "Запускается словом go перед функцией.");
}

// Подчёркивание внутри слова — не курсив, его не трогаем.
static void test_snake_case() {
  const auto l = parse("Файл my_file_name.go лежит рядом.");
  CHECK_STR_EQ(l[0].text, "Файл my_file_name.go лежит рядом.");
}

// Блок кода помечается, а строки с ``` из текста уходят.
static void test_code_block() {
  const auto l = parse(
      "Вот пример:\n"
      "```go\n"
      "func main() {\n"
      "    fmt.Println(\"привет\")\n"
      "}\n"
      "```\n"
      "Готово.");
  CHECK(l.size() == 5);
  CHECK_STR_EQ(l[0].text, "Вот пример:");
  CHECK(!l[0].code);
  CHECK_STR_EQ(l[1].text, "func main() {");
  CHECK(l[1].code);
  CHECK_STR_EQ(l[2].text, "    fmt.Println(\"привет\")");
  CHECK(l[2].code);
  CHECK_STR_EQ(l[3].text, "}");
  CHECK(l[3].code);
  CHECK_STR_EQ(l[4].text, "Готово.");
  CHECK(!l[4].code);
}

// Заголовок теряет решётки, но текст остаётся.
static void test_header() {
  const auto l = parse("## Что такое Go\nЭто язык.");
  CHECK_STR_EQ(l[0].text, "Что такое Go");
  CHECK_STR_EQ(l[1].text, "Это язык.");
}

// Пункты списка приводятся к дефису: точки в шрифте нет.
static void test_list() {
  const auto l = parse("* первый\n+ второй\n- третий");
  CHECK(l.size() == 3);
  CHECK_STR_EQ(l[0].text, "- первый");
  CHECK_STR_EQ(l[1].text, "- второй");
  CHECK_STR_EQ(l[2].text, "- третий");
}

// Внутри блока кода маркеры не трогаем: это код, а не разметка.
static void test_code_keeps_markers() {
  const auto l = parse("```\nx = a * b;\n```");
  CHECK(l.size() == 1);
  CHECK_STR_EQ(l[0].text, "x = a * b;");
  CHECK(l[0].code);
}

// Пустой хвост не оставляет пустых строк.
static void test_trailing() {
  const auto l = parse("Ответ.\n\n\n");
  CHECK(l.size() == 1);
  CHECK_STR_EQ(l[0].text, "Ответ.");
}

// Пустой вход — пустой выход, без падений.
static void test_empty() {
  const auto l = parse("");
  CHECK(l.empty());
}

int main() {
  test_inline();
  test_inline_code();
  test_snake_case();
  test_code_block();
  test_header();
  test_list();
  test_code_keeps_markers();
  test_trailing();
  test_empty();
  return puretest::summary("markdown");
}
