// Разбор HTTP-ответа, полученного вручную через сокет.
// HTTPClient тут не используется: его потоковый путь сдаётся после одной
// короткой записи в сокет, а нам нужна терпеливая отправка.
//
// Модуль чистый — компилируется и тестируется на Mac.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace httpr {

// Код из строки "HTTP/1.1 200 OK" -> 200. Возвращает -1, если не разобрать.
inline int parseStatus(const std::string& raw) {
  const size_t sp = raw.find(' ');
  if (sp == std::string::npos) return -1;
  size_t i = sp + 1;
  while (i < raw.size() && raw[i] == ' ') ++i;
  int code = 0;
  size_t digits = 0;
  while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9') {
    code = code * 10 + (raw[i] - '0');
    ++i;
    ++digits;
  }
  if (digits != 3) return -1;
  return code;
}

// Разделяет ответ на заголовки и тело по пустой строке.
// Если разделителя нет — headers пуст, body = весь ответ.
inline void splitHeadBody(const std::string& raw, std::string& headers,
                          std::string& body) {
  const size_t sep = raw.find("\r\n\r\n");
  if (sep == std::string::npos) {
    headers.clear();
    body = raw;
    return;
  }
  headers = raw.substr(0, sep);
  body = raw.substr(sep + 4);
}

// Есть ли в заголовках Transfer-Encoding: chunked.
inline bool isChunked(const std::string& headers) {
  std::string lower;
  lower.reserve(headers.size());
  for (char c : headers) {
    lower.push_back((char)((c >= 'A' && c <= 'Z') ? c + 32 : c));
  }
  return lower.find("transfer-encoding: chunked") != std::string::npos;
}

// Раскодирует chunked-тело. При любой некорректности возвращает то,
// что успел собрать, — частичный текст лучше пустоты.
inline std::string dechunk(const std::string& body) {
  std::string out;
  size_t i = 0;
  while (i < body.size()) {
    // Размер куска в hex до CRLF
    const size_t eol = body.find("\r\n", i);
    if (eol == std::string::npos) break;
    uint32_t size = 0;
    bool any = false;
    for (size_t k = i; k < eol; ++k) {
      const char c = body[k];
      uint32_t d;
      if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
      else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
      else break;  // расширения chunked игнорируем
      size = (size << 4) | d;
      any = true;
    }
    if (!any) break;
    if (size == 0) break;  // последний кусок
    const size_t start = eol + 2;
    if (start + size > body.size()) {
      out.append(body, start, body.size() - start);  // обрыв — отдаём что есть
      break;
    }
    out.append(body, start, size);
    i = start + size + 2;  // пропустить CRLF после куска
  }
  return out;
}

}  // namespace httpr
