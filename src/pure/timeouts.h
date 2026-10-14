// Таймауты, которые выбирает пользователь: когда гаснет экран, когда
// отключается Wi-Fi, когда стик засыпает и когда уходит в глубокий сон.
// Плюс времена удержаний кнопки: «назад» и сброс сессии.
//
// Таблицы одинаковые по устройству, поэтому живут в одном месте: подпись для
// меню и значение в миллисекундах. Ноль означает «никогда» — по такому
// значению таймер не срабатывает.
//
// Модуль чистый: ни Arduino, ни NVS, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tmo {

struct Choice {
  const char* label;  // подпись в меню
  uint32_t ms;        // значение; 0 — никогда
};

struct Table {
  const Choice* items;
  size_t count;
  size_t def;  // заводское значение
};

// --- этапы сна, по возрастанию простоя ---
constexpr Choice kScreen[] = {{"5 с", 5000},   {"10 с", 10000}, {"20 с", 20000},
                              {"30 с", 30000}, {"1 мин", 60000}};
constexpr Choice kWifi[] = {{"30 с", 30000},  {"1 мин", 60000},
                            {"90 с", 90000},  {"3 мин", 180000},
                            {"никогда", 0}};
constexpr Choice kLight[] = {{"1 мин", 60000},  {"2 мин", 120000},
                             {"5 мин", 300000}, {"10 мин", 600000},
                             {"никогда", 0}};
constexpr Choice kDeep[] = {{"2 мин", 120000},   {"10 мин", 600000},
                            {"30 мин", 1800000}, {"1 час", 3600000},
                            {"никогда", 0}};

// --- времена удержаний ---
constexpr Choice kBack[] = {{"0.5 с", 500}, {"0.85 с", 850}, {"1.5 с", 1500}};
constexpr Choice kReset[] = {{"1.5 с", 1500}, {"3 с", 3000}, {"5 с", 5000}};

inline Table screen() { return {kScreen, 5, 2}; }  // 20 с
inline Table wifi() { return {kWifi, 5, 2}; }      // 90 с
inline Table light() { return {kLight, 5, 1}; }    // 2 мин
inline Table deep() { return {kDeep, 5, 3}; }      // 1 час
inline Table back() { return {kBack, 3, 1}; }      // 850 мс
inline Table reset() { return {kReset, 3, 1}; }    // 3 с

inline bool valid(const Table& t, size_t v) { return v < t.count; }

inline size_t clamp(const Table& t, size_t v) {
  return v < t.count ? v : t.def;
}

inline const char* label(const Table& t, size_t v) {
  return t.items[clamp(t, v)].label;
}

inline uint32_t ms(const Table& t, size_t v) {
  return t.items[clamp(t, v)].ms;
}

inline size_t next(const Table& t, size_t v) {
  return (clamp(t, v) + 1) % t.count;
}

}  // namespace tmo
