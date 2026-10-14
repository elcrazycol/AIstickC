// Сколько стик ждёт перед глубоким сном.
//
// Глубокий сон экономит в тысячи раз больше, чем простой с погашенным
// экраном (единицы микроампер против десятков миллиампер), но память при
// нём теряется: диалог обнуляется. Настройки и счётчик сообщений живут в
// NVS, поэтому они переживают сон.
//
// Поэтому выбор — за пользователем: кому история не нужна, тот ставит
// пару минут и живёт неделями, кому нужна — час или «никогда».
//
// Модуль чистый: ни Arduino, ни NVS, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sleepopt {

// Ступени в секундах. 0 — «никогда», глубокий сон не включается.
constexpr uint32_t kSteps[] = {120, 600, 1800, 3600, 0};
constexpr size_t kStepsCount = sizeof(kSteps) / sizeof(kSteps[0]);
constexpr size_t kDefault = 3;  // час — как было до настройки

constexpr const char* kLabels[] = {"2 мин", "10 мин", "30 мин", "1 час",
                                   "никогда"};

inline size_t clamp(size_t v) { return v < kStepsCount ? v : kDefault; }

inline const char* label(size_t v) { return kLabels[clamp(v)]; }

inline uint32_t seconds(size_t v) { return kSteps[clamp(v)]; }

// Миллисекунды для таймера; 0 — не спать никогда.
inline uint32_t ms(size_t v) { return seconds(v) * 1000u; }

inline size_t next(size_t v) { return (clamp(v) + 1) % kStepsCount; }

inline bool valid(size_t v) { return v < kStepsCount; }

}  // namespace sleepopt
