// Запись JSON: экранирование строк.
//
// Панель отдаёт историю диалога странице, а в ответах модели есть кавычки,
// переводы строк, звёздочки, обратные слэши и кириллица. Собрать JSON склейкой
// строк нельзя: одна кавычка в ответе ломает разбор на стороне браузера.
//
// Модуль чистый: ни Arduino, ни сети, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace jsonw {

// Экранирует строку по правилам JSON и ставит её в кавычки.
inline void quoted(const std::string& in, std::string& out) {
  out.push_back('"');
  for (size_t i = 0; i < in.size(); ++i) {
    const unsigned char c = (unsigned char)in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          // Управляющие символы в JSON допустимы только как \u00XX.
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          // Кириллица — обычный UTF-8, её экранировать не надо.
          out.push_back((char)c);
        }
    }
  }
  out.push_back('"');
}

// Одна реплика диалога: {"u":true,"t":"..."} — u = от пользователя.
inline void message(bool fromUser, const std::string& text, std::string& out) {
  out += "{\"u\":";
  out += fromUser ? "true" : "false";
  out += ",\"t\":";
  quoted(text, out);
  out += "}";
}

}  // namespace jsonw
