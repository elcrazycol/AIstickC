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
#include <vector>

namespace llmreq {

// Одна реплика чата.
struct Msg {
  std::string role;   // "system" | "user" | "assistant"
  std::string text;
};

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

// Тело запроса chat/completions из произвольного набора реплик. Порядок
// ключей: model, messages, max_tokens[, stream][, reasoning_effort][, tools].
// Роли — "system", "user", "assistant".
inline std::string buildChat(const char* model, const std::vector<Msg>& msgs,
                             int maxTokens, bool stream = false,
                             const char* reasoningEffort = nullptr,
                             const char* toolsJson = nullptr) {
  std::string out;
  out.reserve(320 + msgs.size() * 32);

  out += "{\"model\":\"";
  appendEscaped(out, model);
  out += "\",\"messages\":[";
  for (size_t i = 0; i < msgs.size(); ++i) {
    if (i != 0) out += ',';
    out += "{\"role\":\"";
    appendEscaped(out, msgs[i].role.c_str());
    out += "\",\"content\":\"";
    appendEscaped(out, msgs[i].text.c_str());
    out += "\"}";
  }
  out += "],\"max_tokens\":";
  out += std::to_string(maxTokens);
  if (stream) out += ",\"stream\":true";
  if (reasoningEffort != nullptr && reasoningEffort[0] != 0) {
    out += ",\"reasoning_effort\":\"";
    appendEscaped(out, reasoningEffort);
    out += '"';
  }
  if (toolsJson != nullptr && toolsJson[0] != 0) {
    out += ",\"tools\":";
    out += toolsJson;
    out += ",\"tool_choice\":\"auto\"";
  }
  out += '}';
  return out;
}

// Продолжение диалога после вызова инструмента: к истории добавляется
// сообщение ассистента с tool_calls и сообщение role:"tool" с результатом.
// Так модель видит, что сделал плагин, и может это прокомментировать.
inline std::string buildToolFollowUp(const char* model,
                                     const std::vector<Msg>& msgs,
                                     const char* callId, const char* fnName,
                                     const char* argsJson, const char* result,
                                     int maxTokens, bool stream = true,
                                     const char* reasoningEffort = nullptr) {
  std::string out = "{\"model\":\"";
  appendEscaped(out, model);
  out += "\",\"messages\":[";
  for (size_t i = 0; i < msgs.size(); ++i) {
    if (i != 0) out += ',';
    out += "{\"role\":\"";
    appendEscaped(out, msgs[i].role.c_str());
    out += "\",\"content\":\"";
    appendEscaped(out, msgs[i].text.c_str());
    out += "\"}";
  }
  if (!msgs.empty()) out += ',';

  out += "{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"";
  appendEscaped(out, callId);
  out += "\",\"type\":\"function\",\"function\":{\"name\":\"";
  appendEscaped(out, fnName);
  out += "\",\"arguments\":\"";
  appendEscaped(out, argsJson);
  out += "\"}}]}";

  out += ",{\"role\":\"tool\",\"tool_call_id\":\"";
  appendEscaped(out, callId);
  out += "\",\"content\":\"";
  appendEscaped(out, result);
  out += "\"}]";

  out += ",\"max_tokens\":";
  out += std::to_string(maxTokens);
  if (stream) out += ",\"stream\":true";
  if (reasoningEffort != nullptr && reasoningEffort[0] != 0) {
    out += ",\"reasoning_effort\":\"";
    appendEscaped(out, reasoningEffort);
    out += '"';
  }
  out += '}';
  return out;
}

// Тело запроса chat/completions. Порядок ключей совпадает со спекой:
// model, messages (system, user), max_tokens.
inline std::string build(const char* model, const char* system,
                         const char* user, int maxTokens) {
  const std::vector<Msg> msgs = {{"system", system}, {"user", user}};
  return buildChat(model, msgs, maxTokens);
}

}  // namespace llmreq
