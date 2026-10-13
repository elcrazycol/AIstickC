#include "pure_test.h"
#include "chat_options.h"

#include <string>
#include <vector>

using chat::Msg;
using chatopt::Stats;

static std::vector<Msg> dialog(std::initializer_list<std::pair<const char*, const char*>> turns) {
  std::vector<Msg> out;
  for (const auto& t : turns) {
    out.push_back({true, t.first});
    out.push_back({false, t.second});
  }
  return out;
}

// Статистика: средний обмен и средний ответ.
//
// Строки латиницей, чтобы байты совпадали с символами и арифметику можно
// было проверить глазами. Кириллица в UTF-8 двухбайтовая — это отдельная
// проверка ниже.
static void test_stats() {
  const std::vector<Msg> msgs = dialog({
      {"ab", "cdef"},    // обмен 2 + 4 = 6, ответ 4
      {"ghi", "j"},      // обмен 3 + 1 = 4, ответ 1
  });
  const Stats s = chatopt::stats(msgs);
  CHECK(s.turns == 2);
  CHECK(s.answers == 2);
  CHECK(s.avgTurnChars == 5);     // (6 + 4) / 2
  CHECK(s.avgAnswerChars == 2);   // (4 + 1) / 2
}

// Кириллица считается в байтах: «да» — это 4, а не 2.
static void test_stats_cyrillic_bytes() {
  const std::vector<Msg> msgs = dialog({{"да", "нет"}});
  const Stats s = chatopt::stats(msgs);
  CHECK(s.avgTurnChars == 10);   // «да» 4 + «нет» 6
  CHECK(s.avgAnswerChars == 6);  // «нет»
}

// Незакрытый обмен (вопрос без ответа) тоже считается.
static void test_stats_unanswered() {
  std::vector<Msg> msgs = dialog({{"hi", "hello"}});
  msgs.push_back({true, "x"});
  const Stats s = chatopt::stats(msgs);
  CHECK(s.turns == 2);
  CHECK(s.answers == 1);
  CHECK(s.avgAnswerChars == 5);
}

// Пустая история — нули, а не деление на ноль.
static void test_stats_empty() {
  const Stats s = chatopt::stats({});
  CHECK(s.turns == 0);
  CHECK(s.avgTurnChars == 0);
  CHECK(s.avgAnswerChars == 0);
}

// Авто-память: бюджет делим на средний обмен.
static void test_auto_turns() {
  CHECK(chatopt::autoTurns(0, 2400, 5) == 5);       // не по чему подбирать
  CHECK(chatopt::autoTurns(300, 2400, 5) == 8);     // 2400 / 300
  CHECK(chatopt::autoTurns(100, 2400, 5) == 12);    // упёрлись в потолок
  CHECK(chatopt::autoTurns(5000, 2400, 5) == 1);    // один обмен всё равно держим
  CHECK(chatopt::autoTurns(1200, 2400, 5) == 2);
}

// Длина ответа: три уровня, лимит считается из заявленного числа экранов.
static void test_lengths() {
  CHECK(chatopt::kLengthsCount == 3);
  CHECK_STR_EQ(chatopt::lengthLabel(0), "кратко");
  CHECK_STR_EQ(chatopt::lengthLabel(1), "обычно");
  CHECK_STR_EQ(chatopt::lengthLabel(2), "подробно");

  for (size_t i = 1; i < chatopt::kLengthsCount; ++i) {
    CHECK(chatopt::lengthPages(i) > chatopt::lengthPages(i - 1));
    CHECK(chatopt::lengthMaxTokens(i) > chatopt::lengthMaxTokens(i - 1));
  }

  // Лимит вмещает заявленные экраны с запасом, но не в разы больше: иначе
  // модель дописывает до потолка, и ответ раздувается — ровно то, на что
  // жаловались («обычно» давало 20+ экранов при лимите 1200).
  for (size_t i = 0; i < chatopt::kLengthsCount; ++i) {
    const size_t capChars =
        (size_t)chatopt::lengthMaxTokens(i) * chatopt::kCharsPerTokenX10 / 10;
    CHECK(capChars >= chatopt::lengthTarget(i));
    CHECK(capChars <= chatopt::lengthTarget(i) * 3 / 2);
  }

  // Целевые размеры: «обычно» — 4-5 экранов, «подробно» — около 10.
  CHECK(chatopt::lengthPages(1) == 5);    // «обычно» — 5 экранов
  CHECK(chatopt::lengthPages(2) == 10);   // «подробно» — 10 экранов

  // У «кратко» потолок всё равно должен быть заметно выше двух-трёх
  // предложений, иначе ответ начнёт обрываться.
  CHECK(chatopt::lengthTarget(0) >= 300);
}

// Страница — это 5 строк по ~29 знаков.
static void test_page_math() {
  CHECK(chatopt::kCharsPerPage == 145);
  CHECK(chatopt::tokensForPages(1) == 25);    // 145 знаков / 6
  CHECK(chatopt::tokensForPages(10) == 242);  // 1450 / 6
  // Перевод в обе стороны согласован.
  for (int p = 1; p <= 15; ++p) {
    const int t = chatopt::tokensForPages(p);
    CHECK((size_t)t * chatopt::kCharsPerTokenX10 / 10 >=
          (size_t)p * chatopt::kCharsPerPage);
  }
}

// Уровень вне списка (мусор в NVS) — «кратко», а не падение.
static void test_length_clamp() {
  CHECK(chatopt::clampLength(99) == chatopt::kDefaultLength);
  CHECK_STR_EQ(chatopt::lengthLabel(99), "кратко");
  CHECK(chatopt::nextLength(0) == 1);
  CHECK(chatopt::nextLength(2) == 0);   // по кругу
  CHECK(chatopt::nextLength(99) == 1);  // мусор -> кратко -> обычно
}

// Переключение ступеней по кругу, включая возврат к авто.
static void test_next_step() {
  CHECK(chatopt::nextStep(chatopt::kTurnSteps, chatopt::kTurnStepsCount, 0) == 1);
  CHECK(chatopt::nextStep(chatopt::kTurnSteps, chatopt::kTurnStepsCount, 5) == 8);
  CHECK(chatopt::nextStep(chatopt::kTurnSteps, chatopt::kTurnStepsCount, 12) == 0);
  CHECK(chatopt::nextStep(chatopt::kTokenSteps, chatopt::kTokenStepsCount, 0) == 150);
  CHECK(chatopt::nextStep(chatopt::kTokenSteps, chatopt::kTokenStepsCount, 1200) == 2400);
  CHECK(chatopt::nextStep(chatopt::kTokenSteps, chatopt::kTokenStepsCount, 2400) == 0);
  // Значение не из списка (например, после смены прошивки) — начинаем сначала.
  CHECK(chatopt::nextStep(chatopt::kTurnSteps, chatopt::kTurnStepsCount, 7) == 0);
}

// Проверка «значение из списка»: чужой мусор в NVS не должен ломать настройки.
static void test_valid() {
  CHECK(chatopt::isValidTurns(0));      // авто
  CHECK(chatopt::isValidTurns(5));
  CHECK(!chatopt::isValidTurns(7));     // нет такой ступени
  CHECK(chatopt::isValidTokens(0));
  CHECK(chatopt::isValidTokens(600));
  CHECK(!chatopt::isValidTokens(999));
}

// Все ступени в списках действительно существуют и первая — авто.
static void test_steps_sane() {
  CHECK(chatopt::kTurnSteps[0] == 0);
  CHECK(chatopt::kTokenSteps[0] == 0);
  CHECK(chatopt::kTurnStepsCount == 6);
  CHECK(chatopt::kTokenStepsCount == 6);
  for (size_t i = 1; i < chatopt::kTurnStepsCount; ++i) {
    CHECK(chatopt::kTurnSteps[i] <= chatopt::kAutoTurnsMax);
  }
  // Ступени по возрастанию. Потолок авто (1500) может быть ниже ручной
  // ступени 2000: авто осторожничает, а человек вправе попросить больше.
  // Ступени по возрастанию, и верхняя не ниже самого длинного уровня:
  // иначе ручной выбор не сможет дать столько же, сколько «авто».
  for (size_t i = 1; i < chatopt::kTokenStepsCount; ++i) {
    CHECK(chatopt::kTokenSteps[i] >= chatopt::kAutoTokensMin);
    CHECK(chatopt::kTokenSteps[i] > chatopt::kTokenSteps[i - 1]);
  }
  CHECK((size_t)chatopt::kTokenSteps[chatopt::kTokenStepsCount - 1] >=
        (size_t)chatopt::lengthMaxTokens(chatopt::kLengthsCount - 1));
}

int main() {
  test_stats();
  test_stats_cyrillic_bytes();
  test_stats_unanswered();
  test_stats_empty();
  test_auto_turns();
  test_lengths();
  test_page_math();
  test_length_clamp();
  test_next_step();
  test_valid();
  test_steps_sane();
  return puretest::summary("chat_options");
}
