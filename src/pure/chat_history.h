// История диалога: последние пары «вопрос-ответ» с обрезкой по бюджету.
//
// Модуль чистый. Память сессионная: живёт в куче до выключения устройства,
// во флеш ничего не пишется.
//
// Обрезка нужна, чтобы запрос к модели не раздувался: длинный контекст
// дороже и отвечает медленнее. Самый старый обмен удаляется первым, свежий
// сохраняется всегда.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace chat {

struct Msg {
  bool fromUser;      // true — реплика пользователя, false — ответ модели
  std::string text;
};

class History {
 public:
  explicit History(size_t maxTurns = 5) : maxTurns_(maxTurns ? maxTurns : 1) {}

  void clear() { msgs_.clear(); }

  void addUser(const char* text) { msgs_.push_back({true, text ? text : ""}); }
  void addAssistant(const char* text) {
    msgs_.push_back({false, text ? text : ""});
  }

  const std::vector<Msg>& messages() const { return msgs_; }

  // Число обменов — по числу реплик пользователя.
  size_t turnCount() const {
    size_t n = 0;
    for (const Msg& m : msgs_) {
      if (m.fromUser) ++n;
    }
    return n;
  }

  size_t totalChars() const {
    size_t n = 0;
    for (const Msg& m : msgs_) n += m.text.size();
    return n;
  }

  // Оставляет не более maxTurns_ обменов и maxChars символов суммарно.
  void trim(size_t maxChars) {
    while (turnCount() > maxTurns_) dropOldestTurn();
    // Последний обмен не удаляем, даже если он один не влезает в бюджет.
    while (totalChars() > maxChars && turnCount() > 1) dropOldestTurn();
  }

 private:
  void dropOldestTurn() {
    if (msgs_.empty()) return;
    msgs_.erase(msgs_.begin());  // реплика пользователя
    if (!msgs_.empty() && !msgs_.front().fromUser) {
      msgs_.erase(msgs_.begin());  // её ответ
    }
  }

  std::vector<Msg> msgs_;
  size_t maxTurns_;
};

}  // namespace chat
