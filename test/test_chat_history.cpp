#include "pure_test.h"
#include "chat_history.h"

#include <string>

static void test_empty() {
  chat::History h(5);
  CHECK_EQ(h.turnCount(), (size_t)0);
  CHECK_EQ(h.messages().size(), (size_t)0);
  CHECK_EQ(h.totalChars(), (size_t)0);
}

static void test_add_and_order() {
  chat::History h(5);
  h.addUser("вопрос 1");
  h.addAssistant("ответ 1");
  h.addUser("вопрос 2");
  h.addAssistant("ответ 2");
  CHECK_EQ(h.turnCount(), (size_t)2);
  CHECK_EQ(h.messages().size(), (size_t)4);
  CHECK(h.messages()[0].fromUser);
  CHECK(!h.messages()[1].fromUser);
  CHECK_STR_EQ(h.messages()[0].text, "вопрос 1");
  CHECK_STR_EQ(h.messages()[3].text, "ответ 2");
}

static void test_max_turns_drops_oldest() {
  chat::History h(2);
  for (int i = 1; i <= 4; ++i) {
    h.addUser(("в" + std::to_string(i)).c_str());
    h.addAssistant(("о" + std::to_string(i)).c_str());
    h.trim(100000);
  }
  CHECK_EQ(h.turnCount(), (size_t)2);
  // Остались обмены 3 и 4.
  CHECK_STR_EQ(h.messages().front().text, "в3");
  CHECK_STR_EQ(h.messages().back().text, "о4");
}

static void test_max_chars_drops_oldest() {
  chat::History h(10);
  h.addUser("aaaa");       // 4
  h.addAssistant("bbbb");  // 4
  h.addUser("cccc");       // 4
  h.addAssistant("dddd");  // 4
  CHECK_EQ(h.totalChars(), (size_t)16);
  h.trim(8);  // оставить не больше 8 символов → один обмен
  CHECK_EQ(h.turnCount(), (size_t)1);
  CHECK_STR_EQ(h.messages().front().text, "cccc");
  CHECK_STR_EQ(h.messages().back().text, "dddd");
}

static void test_trim_keeps_last_turn_even_over_budget() {
  chat::History h(10);
  h.addUser("очень длинный вопрос");
  h.addAssistant("очень длинный ответ");
  h.trim(4);  // меньше одного обмена
  CHECK_EQ(h.turnCount(), (size_t)1);
  CHECK_EQ(h.messages().size(), (size_t)2);
}

static void test_trim_keeps_alternation() {
  chat::History h(3);
  for (int i = 0; i < 5; ++i) {
    h.addUser("u");
    h.addAssistant("a");
    h.trim(100000);
  }
  // Первая реплика после обрезки — всегда пользователь.
  CHECK(h.messages().front().fromUser);
  for (size_t i = 0; i < h.messages().size(); ++i) {
    CHECK_EQ(h.messages()[i].fromUser, (i % 2) == 0);
  }
}

static void test_clear() {
  chat::History h(5);
  h.addUser("q");
  h.addAssistant("a");
  h.clear();
  CHECK_EQ(h.turnCount(), (size_t)0);
  CHECK_EQ(h.messages().size(), (size_t)0);
}

static void test_null_text_safe() {
  chat::History h(5);
  h.addUser(nullptr);
  h.addAssistant(nullptr);
  CHECK_EQ(h.turnCount(), (size_t)1);
  CHECK_STR_EQ(h.messages()[0].text, "");
}

static void test_zero_max_turns_becomes_one() {
  chat::History h(0);
  h.addUser("1");
  h.addAssistant("1");
  h.addUser("2");
  h.addAssistant("2");
  h.trim(100000);
  CHECK_EQ(h.turnCount(), (size_t)1);
  CHECK_STR_EQ(h.messages().front().text, "2");
}

// Лимит можно сменить на ходу — так работает настройка «сколько помнить».
static void test_set_max_turns() {
  chat::History h(5);
  for (int i = 1; i <= 5; ++i) {
    h.addUser("в");
    h.addAssistant("о");
  }
  CHECK_EQ(h.turnCount(), (size_t)5);

  h.setMaxTurns(2);
  h.trim(100000);
  CHECK_EQ(h.turnCount(), (size_t)2);

  // Расширение лимита само по себе ничего не добавляет: старое уже удалено.
  h.setMaxTurns(8);
  h.trim(100000);
  CHECK_EQ(h.turnCount(), (size_t)2);
  CHECK_EQ(h.maxTurns(), (size_t)8);
}

int main() {
  test_empty();
  test_add_and_order();
  test_max_turns_drops_oldest();
  test_max_chars_drops_oldest();
  test_trim_keeps_last_turn_even_over_budget();
  test_trim_keeps_alternation();
  test_clear();
  test_null_text_safe();
  test_zero_max_turns_becomes_one();
  test_set_max_turns();
  return puretest::summary("chat_history");
}
