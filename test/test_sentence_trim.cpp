#include "pure_test.h"
#include "sentence_trim.h"

#include <string>

// Обрыв на полуслове — режем до последней точки.
static void test_cut_at_period() {
  std::string t = "Go — компилируемый язык. Его любят за простоту. Популярен для сервер";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Go — компилируемый язык. Его любят за простоту.");
}

// Ответ уже закончен точкой — не трогаем.
static void test_already_complete() {
  std::string t = "Go — компилируемый язык.";
  CHECK(!trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Go — компилируемый язык.");
}

// Десятичная дробь концом предложения не считается: режем по настоящей точке.
static void test_decimal() {
  std::string t = "Вышла версия 3.14 в две тыся";
  CHECK(trim::trimToSentence(t));
  // Точки-конца нет вовсе — режем по последнему пробелу.
  CHECK_STR_EQ(t, "Вышла версия 3.14 в две…");
}

// Точка внутри сокращения посреди незаконченной фразы — не конец предложения.
static void test_abbreviation() {
  std::string t = "Языки, библиотеки и т.д. Он стал популя";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Языки, библиотеки и т.д.");
}

// Многоточие — тоже конец предложения.
static void test_ellipsis() {
  std::string t = "Он подумал… и добавил ещё сл";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Он подумал…");
}

// Закрывающая кавычка после точки сохраняется, хвост уходит.
static void test_trailing_quote() {
  std::string t = "Он сказал «привет». Потом пошёл домо";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Он сказал «привет».");
}

// Восклицательный и вопросительный знаки тоже считаются.
static void test_other_marks() {
  std::string a = "Правда? Конечно! Но всё ж";
  CHECK(trim::trimToSentence(a));
  CHECK_STR_EQ(a, "Правда? Конечно!");
}

// Конца предложения нет, но текст длинный — режем по пробелу.
static void test_no_end_mark() {
  std::string t = "Это очень длинная фраза без единой точки и она обрывается на полусл";
  const size_t was = t.size();
  CHECK(trim::trimToSentence(t));
  CHECK(t.size() < was);
  CHECK((unsigned char)t.back() == 0xA6);  // последний байт многоточия
}

// Обрыв внутри примера кода: режем до примера, а не посреди него.
static void test_code_sample() {
  std::string t = "Пример на Go. func main() { fmt.Println(\"hi\"";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Пример на Go.");
}

// Закрытый пример кода подрезке не мешает.
static void test_code_closed() {
  std::string t = "Пример на Go. func main() { fmt.Println(\"hi\") } Потом ещё текс";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Пример на Go. func main() { fmt.Println(\"hi\") }");
}

// Незакрытая кавычка кода — тоже режем раньше.
static void test_code_fence() {
  std::string t = "Вот пример. ```go\nfmt.Println(1)\n```\nИ ещё немно";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Вот пример. ```go\nfmt.Println(1)\n```");
}

// Короткий текст без конца предложения не трогаем: испортить хуже.
static void test_short_untouched() {
  std::string t = "Привет";
  CHECK(!trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Привет");
}

// Пустая строка — ничего не делаем.
static void test_empty() {
  std::string t;
  CHECK(!trim::trimToSentence(t));
  CHECK(t.empty());
}

// Хвост из пробелов после точки убирается, лишних пробелов не остаётся.
static void test_trailing_spaces() {
  std::string t = "Первое. Второе.   Треть";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Первое. Второе.");
}

// Хвостовой пробел после точки убираем, сам текст не портим.
static void test_period_at_end_with_space() {
  std::string t = "Готово. ";
  CHECK(trim::trimToSentence(t));
  CHECK_STR_EQ(t, "Готово.");
}

int main() {
  test_cut_at_period();
  test_already_complete();
  test_decimal();
  test_abbreviation();
  test_ellipsis();
  test_trailing_quote();
  test_other_marks();
  test_code_sample();
  test_code_closed();
  test_code_fence();
  test_no_end_mark();
  test_short_untouched();
  test_empty();
  test_trailing_spaces();
  test_period_at_end_with_space();
  return puretest::summary("sentence_trim");
}
