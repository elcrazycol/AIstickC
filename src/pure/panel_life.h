// Когда панель обязана закрыться.
//
// Панель — это открытый порт и включённое радио: пока она жива, стик не
// уходит в лёгкий сон, а в сети висит лишняя дверь. Поэтому жить она должна
// ровно столько, сколько ею пользуются, и закрываться по любому поводу:
//
//   - нет запросов дольше отведённого (страницу закрыли, вкладку свернули,
//     браузер упал — запросы просто перестали приходить);
//   - прошёл общий потолок с момента открытия (даже если запросы идут:
//     забытая в фоне вкладка не должна держать панель сутками).
//
// Модуль чистый: ни Arduino, ни сети, проверяется обычными тестами.
#pragma once

#include <cstdint>

namespace panellife {

struct Policy {
  uint32_t idleMs;  // нет запросов столько — закрыть
  uint32_t maxMs;   // с момента открытия — закрыть безусловно

  Policy(uint32_t idle = 120000, uint32_t max = 900000)
      : idleMs(idle), maxMs(max) {}

  bool idleExpired(uint32_t now, uint32_t lastRequestAt) const {
    return (uint32_t)(now - lastRequestAt) >= idleMs;
  }

  bool maxExpired(uint32_t now, uint32_t openedAt) const {
    return (uint32_t)(now - openedAt) >= maxMs;
  }

  // Общий ответ: закрываться ли прямо сейчас.
  bool shouldClose(uint32_t now, uint32_t openedAt,
                   uint32_t lastRequestAt) const {
    return idleExpired(now, lastRequestAt) || maxExpired(now, openedAt);
  }
};

}  // namespace panellife
