// Функции, доступные скрипту-плагину.
//
// Это и есть вся «поверхность атаки» скачанного плагина: список ниже —
// исчерпывающий. Ни файлов, ни произвольных пинов, ни секретов у скрипта нет.
//
//   log(текст)                        — в Serial
//   show(текст)                       — панель на экране стика
//   answer(текст)                     — длинный ответ (как ответ модели)
//   ask(текст)                        — уточняющий вопрос пользователю
//   len(s) / str(x) / num(x) / contains(s,sub)
//   json(текст, путь)                 — вытащить поле ("state" или "a/[0]/b")
//   gpio(пин, значение) / gpio_get(пин) — только из белого списка
//   sleep(мс)                         — с ограничением
//   http(метод, url[, тело])          — вернёт тело ответа
//   proxy_post(путь, тело)            — POST на наш прокси с X-Auth (секрет скрыт)
//   weather_text(строка wttr.in)      — русская строка погоды
#pragma once

#include <string>
#include <vector>

#include "pure/script.h"

namespace scripthost {

class Host : public script::Host {
 public:
  bool call(const std::string& name, const std::vector<script::Value>& args,
            script::Value& out, std::string& err) override;

  // Что скрипт показал: show(), answer() или ask().
  const std::string& result() const { return result_; }
  bool asAnswer() const { return asAnswer_; }
  bool asked() const { return asked_; }

 private:
  std::string result_;
  bool asAnswer_ = false;
  bool asked_ = false;
};

}  // namespace scripthost
