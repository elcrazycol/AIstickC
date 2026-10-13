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

// Ступени лимита ответа: 0 — авто (следует за длиной ответа), дальше токены.
constexpr int kTokenSteps[] = {0, 300, 600, 1000, 2000, 3000};
constexpr size_t kTokenStepsCount = sizeof(kTokenSteps) / sizeof(kTokenSteps[0]);

// Границы, за которые авто не выходит.
constexpr size_t kAutoTurnsMin = 1;
constexpr size_t kAutoTurnsMax = 12;
constexpr int    kAutoTokensMin = 300;
constexpr int    kAutoTokensMax = 3000;

// --- Длина ответа ---
//
// Три уровня: «кратко» — то, что было с самого начала, дальше просим больше
// текста. Вместе с просьбой растёт и лимит токенов: он должен быть с запасом,
// иначе длинный ответ оборвётся на полуслове.
//
// targetChars — сколько знаков просим. Экран показывает 5 строк по ~29
// знаков, то есть страница ≈ 145 знаков: 3-4 страницы ≈ 500, 6-7 ≈ 950.
struct Length {
  const char* label;       // подпись в меню
  size_t targetChars;      // сколько просим у модели
  int maxTokens;           // лимит с запасом
  const char* ask;         // формулировка для системного промпта
};

constexpr Length kLengths[] = {
    {"кратко", 200, 600,
     "Отвечай кратко, максимум 2-3 коротких предложения на языке пользователя."},
    {"обычно", 500, 1200,
     "Отвечай на языке пользователя на 3-4 экрана текста, примерно 10-15 "
     "предложений: по делу, без воды."},
    {"подробно", 950, 2400,
     "Отвечай на языке пользователя подробно, до 6-7 экранов текста: "
     "развёрнуто, с примерами и пояснениями."},
};
constexpr size_t kLengthsCount = sizeof(kLengths) / sizeof(kLengths[0]);
constexpr size_t kDefaultLength = 0;  // кратко — как было изначально

inline size_t clampLength(size_t v) {
  return v < kLengthsCount ? v : kDefaultLength;
}
inline const char* lengthLabel(size_t v) { return kLengths[clampLength(v)].label; }
inline size_t lengthTarget(size_t v) {
  return kLengths[clampLength(v)].targetChars;
}
inline int lengthMaxTokens(size_t v) {
  return kLengths[clampLength(v)].maxTokens;
}
inline const char* lengthAsk(size_t v) { return kLengths[clampLength(v)].ask; }

// Следующий уровень по кругу.
inline size_t nextLength(size_t v) { return (clampLength(v) + 1) % kLengthsCount; }

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
