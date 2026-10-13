// Плагины и tool calling: описание инструментов для модели и разбор её вызова.
//
// Модель сама решает, когда вызвать инструмент. Мы отдаём ей список плагинов
// (имя, описание, JSON-схема аргументов), а в ответ получаем вызов: имя и
// аргументы. Всё это — чистый модуль: сборку JSON и разбор дельт можно
// проверить на Mac без железа.
//
// Формат tools — OpenAI-совместимый:
//   [{"type":"function","function":{"name":...,"description":...,"parameters":{...}}}]
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "json_extract.h"
#include "llm_request.h"  // appendEscaped

namespace toolx {

// Что делает плагин, когда модель его вызвала.
enum class Kind {
  Gpio,    // дёрнуть пин (state: on/off/toggle)
  Screen,  // показать текст (text)
  Http,    // запрос на URL (query — необязательный параметр)
};

struct Plugin {
  const char* name;         // имя функции для модели, латиницей
  const char* description;  // описание для модели: когда вызывать
  const char* parameters;   // JSON-схема аргументов (готовая строка)
  Kind kind;
  int pin;                  // для Gpio
  const char* url;          // для Http
  bool answerAfter;         // спросить ли модель ещё раз после действия
};

// Собирает массив tools из таблицы плагинов. Пустая таблица — пустая строка
// (тогда поле tools в запрос не добавляется).
inline std::string buildTools(const Plugin* plugins, size_t n) {
  if (plugins == nullptr || n == 0) return std::string();
  std::string out = "[";
  for (size_t i = 0; i < n; ++i) {
    if (i != 0) out += ',';
    out += "{\"type\":\"function\",\"function\":{\"name\":\"";
    llmreq::appendEscaped(out, plugins[i].name);
    out += "\",\"description\":\"";
    llmreq::appendEscaped(out, plugins[i].description);
    out += "\",\"parameters\":";
    out += plugins[i].parameters;  // уже валидный JSON
    out += "}}";
  }
  out += ']';
  return out;
}

// Имя функции из дельты стрима (приходит только в первом куске вызова).
inline bool deltaName(const char* deltaJson, std::string& out) {
  return jsonx::extractPath(
      deltaJson, "choices/[0]/delta/tool_calls/[0]/function/name", out);
}

// Идентификатор вызова из дельты стрима (нужен, чтобы вернуть результат
// модели в поле tool_call_id).
inline bool deltaId(const char* deltaJson, std::string& out) {
  return jsonx::extractPath(deltaJson, "choices/[0]/delta/tool_calls/[0]/id",
                            out);
}

// Очередной кусок аргументов из дельты стрима (приходят посимвольно —
// их надо склеивать).
inline bool deltaArgs(const char* deltaJson, std::string& out) {
  return jsonx::extractPath(
      deltaJson, "choices/[0]/delta/tool_calls/[0]/function/arguments", out);
}

// Строковый аргумент из собранного JSON аргументов: {"state": "on"} -> "on".
inline bool argString(const char* argsJson, const char* key, std::string& out) {
  return jsonx::extractPath(argsJson, key, out);
}

}  // namespace toolx
