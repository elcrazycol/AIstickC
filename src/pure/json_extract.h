// Минимальный разбор JSON: вытащить строковое значение по ключу.
// Полноценный парсер тут не нужен и не поместился бы в бюджет.
//
// Гарантии:
//  - ключ ищется только на верхнем уровне, вне строковых значений;
//  - снимается экранирование \" \\ \/ \b \f \n \r \t и \uXXXX (UTF-8);
//  - обрезанный или битый JSON даёт false, а не мусор.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace jsonx {

// Пропускает пробелы.
inline size_t skipWs(const char* s, size_t i) {
  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') ++i;
  return i;
}

// Читает строковый литерал, начиная с кавычки в позиции i.
// Возвращает true и заполняет out; i сдвигается за закрывающую кавычку.
inline bool readString(const char* s, size_t& i, std::string& out) {
  if (s[i] != '"') return false;
  ++i;
  out.clear();
  while (s[i] != 0 && s[i] != '"') {
    if (s[i] != '\\') { out.push_back(s[i++]); continue; }
    ++i;
    const char e = s[i];
    switch (e) {
      case '"':  out.push_back('"');  ++i; break;
      case '\\': out.push_back('\\'); ++i; break;
      case '/':  out.push_back('/');  ++i; break;
      case 'b':  out.push_back('\b'); ++i; break;
      case 'f':  out.push_back('\f'); ++i; break;
      case 'n':  out.push_back('\n'); ++i; break;
      case 'r':  out.push_back('\r'); ++i; break;
      case 't':  out.push_back('\t'); ++i; break;
      case 'u': {
        // \uXXXX -> UTF-8. Суррогатные пары не поддержаны: для ответов
        // Whisper этого достаточно, а полноценность тут не нужна.
        ++i;
        uint32_t cp = 0;
        for (int k = 0; k < 4; ++k) {
          const char h = s[i];
          uint32_t d;
          if (h >= '0' && h <= '9') d = (uint32_t)(h - '0');
          else if (h >= 'a' && h <= 'f') d = (uint32_t)(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') d = (uint32_t)(h - 'A' + 10);
          else return false;
          cp = (cp << 4) | d;
          ++i;
        }
        if (cp < 0x80) {
          out.push_back((char)cp);
        } else if (cp < 0x800) {
          out.push_back((char)(0xC0 | (cp >> 6)));
          out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
          out.push_back((char)(0xE0 | (cp >> 12)));
          out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        break;
      }
      case 0: return false;
      default: return false;  // неизвестная escape-последовательность
    }
  }
  if (s[i] != '"') return false;  // не закрылось — обрыв
  ++i;
  return true;
}

// Пропускает произвольное значение (строку, число, объект, массив, литерал),
// начиная с позиции i. Нужно, чтобы перешагивать чужие поля верхнего уровня.
inline bool skipValue(const char* s, size_t& i) {
  i = skipWs(s, i);
  const char c = s[i];
  if (c == '"') {
    std::string tmp;
    return readString(s, i, tmp);
  }
  if (c == '{' || c == '[') {
    const char open = c;
    const char close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (s[i] != 0) {
      if (s[i] == '"') {
        std::string tmp;
        if (!readString(s, i, tmp)) return false;
        continue;
      }
      if (s[i] == open) ++depth;
      else if (s[i] == close) {
        --depth;
        ++i;
        if (depth == 0) return true;
        continue;
      }
      ++i;
    }
    return false;
  }
  // число, true, false, null — до запятой или закрывающей скобки
  while (s[i] != 0 && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
  return true;
}

// Ищет ключ на верхнем уровне и возвращает его строковое значение.
inline bool extractString(const char* json, const char* key, std::string& out) {
  size_t i = skipWs(json, 0);
  if (json[i] != '{') return false;
  ++i;

  const size_t keyLen = std::char_traits<char>::length(key);

  while (true) {
    i = skipWs(json, i);
    if (json[i] == '}') return false;      // ключа нет
    if (json[i] == 0) return false;        // обрыв

    std::string name;
    if (!readString(json, i, name)) return false;

    i = skipWs(json, i);
    if (json[i] != ':') return false;
    ++i;
    i = skipWs(json, i);

    const bool hit = (name.size() == keyLen) && (name.compare(key) == 0);
    if (hit) {
      if (json[i] != '"') return false;    // значение не строка
      return readString(json, i, out);
    }
    if (!skipValue(json, i)) return false;

    i = skipWs(json, i);
    if (json[i] == ',') { ++i; continue; }
    if (json[i] == '}') return false;
    return false;
  }
}

}  // namespace jsonx
