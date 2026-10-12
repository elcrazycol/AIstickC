// Сборка тела запроса к OpenAI-совместимому эндпоинту OpenCode Go.
//
// Тело небольшое (системный промпт + распознанная фраза), поэтому, в
// отличие от multipart аудио, собирается целиком в памяти одним буфером.
//
// Модуль чистый — компилируется и тестируется на Mac.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace llmreq {

// Сколько байт займёт строка после экранирования (без обрамляющих кавычек).
inline size_t escapedLen(const char* s) {
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p != 0; ++p) {
    const unsigned char c = *p;
    if (c == '"' || c == '\\') n += 2;
    else if (c == '\n' || c == '\r' || c == '\t' || c == '\b' || c == '\f') n += 2;
    else if (c < 0x20) n += 6;  // \u00XX
    else n += 1;                // в т.ч. байты UTF-8 как есть
  }
  return n;
}

// Пишет экранированную строку в out и возвращает число записанных байт.
// Кавычки не добавляет — их ставит вызывающий.
inline size_t writeEscaped(char* out, const char* s) {
  static const char* kHex = "0123456789abcdef";
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p != 0; ++p) {
    const unsigned char c = *p;
    switch (c) {
      case '"':  out[n++] = '\\'; out[n++] = '"';  break;
      case '\\': out[n++] = '\\'; out[n++] = '\\'; break;
      case '\n': out[n++] = '\\'; out[n++] = 'n';  break;
      case '\r': out[n++] = '\\'; out[n++] = 'r';  break;
      case '\t': out[n++] = '\\'; out[n++] = 't';  break;
      case '\b': out[n++] = '\\'; out[n++] = 'b';  break;
      case '\f': out[n++] = '\\'; out[n++] = 'f';  break;
      default:
        if (c < 0x20) {
          out[n++] = '\\'; out[n++] = 'u'; out[n++] = '0'; out[n++] = '0';
          out[n++] = kHex[(c >> 4) & 0xF];
          out[n++] = kHex[c & 0xF];
        } else {
          out[n++] = (char)c;
        }
    }
  }
  return n;
}

// Дописывает экранированную строку к out.
inline void appendEscaped(std::string& out, const char* s) {
  const size_t n = escapedLen(s);
  if (n == 0) return;
  const size_t at = out.size();
  out.resize(at + n);
  writeEscaped(&out[at], s);
}

// Тело запроса chat/completions. Порядок ключей совпадает со спекой:
// model, messages (system, user), max_tokens.
inline std::string build(const char* model, const char* system,
                         const char* user, int maxTokens) {
  std::string out;
  out.reserve(320 + escapedLen(system) + escapedLen(user));

  out += "{\"model\":\"";
  appendEscaped(out, model);
  out += "\",\"messages\":[{\"role\":\"system\",\"content\":\"";
  appendEscaped(out, system);
  out += "\"},{\"role\":\"user\",\"content\":\"";
  appendEscaped(out, user);
  out += "\"}],\"max_tokens\":";
  out += std::to_string(maxTokens);
  out += '}';
  return out;
}

}  // namespace llmreq
