// Что помнить в диалоге и сколько просить у модели.
//
// Пользователь выбирает ступень в настройках либо оставляет «авто» — тогда
// значения подбираются по факту: сколько символов занимает один обмен и
// сколько модель отвечает обычно. Модуль чистый: ни Arduino, ни NVS, всё
// считается из переданных чисел, поэтому проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <vector>

#include "chat_history.h"

namespace chatopt {

// Ступени памяти: 0 — авто, дальше число обменов.
constexpr size_t kTurnSteps[] = {0, 1, 3, 5, 8, 12};
constexpr size_t kTurnStepsCount = sizeof(kTurnSteps) / sizeof(kTurnSteps[0]);

// Ступени лимита ответа: 0 — авто, дальше токены.
constexpr int kTokenSteps[] = {0, 300, 600, 1000, 2000};
constexpr size_t kTokenStepsCount = sizeof(kTokenSteps) / sizeof(kTokenSteps[0]);

// Границы, за которые авто не выходит.
constexpr size_t kAutoTurnsMin = 1;
constexpr size_t kAutoTurnsMax = 12;
constexpr int    kAutoTokensMin = 300;
constexpr int    kAutoTokensMax = 1500;

// Русский текст — примерно два символа на токен.
constexpr size_t kCharsPerToken = 2;
// Запас к тому, что модель уже отвечает: ответ не должен обрываться на
// полуслове, но и лишнего бюджета просить незачем — он стоит времени.
constexpr int kTokenHeadroomPercent = 160;

// Что известно о текущем диалоге.
struct Stats {
  size_t turns = 0;           // обменов
  size_t answers = 0;         // ответов модели
  size_t avgTurnChars = 0;    // средний обмен (вопрос + ответ), символов
  size_t avgAnswerChars = 0;  // средний ответ, символов
};

inline Stats stats(const std::vector<chat::Msg>& msgs) {
  Stats s;
  size_t turnSum = 0;
  size_t ansSum = 0;
  size_t cur = 0;       // символы текущего обмена
  bool inTurn = false;

  for (const chat::Msg& m : msgs) {
    if (m.fromUser) {
      if (inTurn) {  // закрываем предыдущий обмен
        turnSum += cur;
        ++s.turns;
      }
      cur = m.text.size();
      inTurn = true;
    } else {
      cur += m.text.size();
      ansSum += m.text.size();
      ++s.answers;
    }
  }
  if (inTurn) {
    turnSum += cur;
    ++s.turns;
  }

  s.avgTurnChars = s.turns ? turnSum / s.turns : 0;
  s.avgAnswerChars = s.answers ? ansSum / s.answers : 0;
  return s;
}

// Авто-память: столько обменов, чтобы уложиться в бюджет символов при текущем
// среднем размере обмена. История пуста — отдаём defaultTurns: подбирать пока
// не по чему.
inline size_t autoTurns(size_t avgTurnChars, size_t budgetChars,
                        size_t defaultTurns) {
  if (avgTurnChars == 0) return defaultTurns;
  size_t n = budgetChars / avgTurnChars;
  if (n < kAutoTurnsMin) n = kAutoTurnsMin;
  if (n > kAutoTurnsMax) n = kAutoTurnsMax;
  return n;
}

// Авто-лимит ответа: с запасом к средней длине ответов модели.
inline int autoTokens(size_t avgAnswerChars, int defaultTokens) {
  if (avgAnswerChars == 0) return defaultTokens;
  const size_t base = avgAnswerChars / kCharsPerToken;
  size_t t = base * (size_t)kTokenHeadroomPercent / 100;
  if (t < (size_t)kAutoTokensMin) t = kAutoTokensMin;
  if (t > (size_t)kAutoTokensMax) t = kAutoTokensMax;
  return (int)t;
}

// Значение из списка? Нужно, чтобы выбор, оставшийся от прошлой прошивки,
// не превратился в мусор.
inline bool contains(const size_t* steps, size_t count, size_t v) {
  for (size_t i = 0; i < count; ++i) {
    if (steps[i] == v) return true;
  }
  return false;
}

inline bool contains(const int* steps, size_t count, int v) {
  for (size_t i = 0; i < count; ++i) {
    if (steps[i] == v) return true;
  }
  return false;
}

inline bool isValidTurns(size_t v) {
  return contains(kTurnSteps, kTurnStepsCount, v);
}

inline bool isValidTokens(int v) {
  return contains(kTokenSteps, kTokenStepsCount, v);
}

// Следующая ступень по кругу — для переключения кнопкой.
inline size_t nextStep(const size_t* steps, size_t count, size_t current) {
  for (size_t i = 0; i < count; ++i) {
    if (steps[i] == current) return steps[(i + 1) % count];
  }
  return steps[0];
}

inline int nextStep(const int* steps, size_t count, int current) {
  for (size_t i = 0; i < count; ++i) {
    if (steps[i] == current) return steps[(i + 1) % count];
  }
  return steps[0];
}

}  // namespace chatopt
