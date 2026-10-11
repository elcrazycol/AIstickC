// Раскладка тела multipart/form-data для Groq.
//
// Формат каждой части:
//   --<boundary>\r\n
//   Content-Disposition: form-data; name="<name>"\r\n
//   \r\n
//   <value>\r\n
//
// У файловой части между заголовком и телом добавляется Content-Type:
//   --<boundary>\r\n
//   Content-Disposition: form-data; name="file"; filename="audio.wav"\r\n
//   Content-Type: audio/wav\r\n
//   \r\n
//   <байты файла>\r\n
//
// Тело закрывается строкой "--<boundary>--\r\n".
//
// Все функции возвращают число записанных байт, чтобы вызывающий мог
// складывать смещения без арифметики на месте.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace mp {

inline size_t cstrLen(const char* s) { return std::strlen(s); }

inline size_t writeRaw(uint8_t* out, const char* s) {
  const size_t n = cstrLen(s);
  std::memcpy(out, s, n);
  return n;
}

// "--<boundary>\r\n"
inline size_t openingSize(const char* boundary) {
  return 2 + cstrLen(boundary) + 2;
}
inline size_t writeOpening(uint8_t* out, const char* boundary) {
  size_t n = 0;
  n += writeRaw(out + n, "--");
  n += writeRaw(out + n, boundary);
  n += writeRaw(out + n, "\r\n");
  return n;
}

// Заголовок обычного поля + значение + CRLF.
inline size_t fieldSize(const char* boundary, const char* name,
                        const char* value) {
  return openingSize(boundary) +
         cstrLen("Content-Disposition: form-data; name=\"") + cstrLen(name) +
         cstrLen("\"\r\n\r\n") + cstrLen(value) + 2;
}

inline size_t writeField(uint8_t* out, const char* boundary, const char* name,
                         const char* value) {
  size_t n = 0;
  n += writeOpening(out + n, boundary);
  n += writeRaw(out + n, "Content-Disposition: form-data; name=\"");
  n += writeRaw(out + n, name);
  n += writeRaw(out + n, "\"\r\n\r\n");
  n += writeRaw(out + n, value);
  n += writeRaw(out + n, "\r\n");
  return n;
}

// Заголовок файловой части — без тела файла и без завершающего CRLF.
inline size_t fileHeaderSize(const char* boundary, const char* name,
                             const char* filename, const char* contentType) {
  return openingSize(boundary) +
         cstrLen("Content-Disposition: form-data; name=\"") + cstrLen(name) +
         cstrLen("\"; filename=\"") + cstrLen(filename) +
         cstrLen("\"\r\nContent-Type: ") + cstrLen(contentType) +
         cstrLen("\r\n\r\n");
}

inline size_t writeFileHeader(uint8_t* out, const char* boundary,
                              const char* name, const char* filename,
                              const char* contentType) {
  size_t n = 0;
  n += writeOpening(out + n, boundary);
  n += writeRaw(out + n, "Content-Disposition: form-data; name=\"");
  n += writeRaw(out + n, name);
  n += writeRaw(out + n, "\"; filename=\"");
  n += writeRaw(out + n, filename);
  n += writeRaw(out + n, "\"\r\nContent-Type: ");
  n += writeRaw(out + n, contentType);
  n += writeRaw(out + n, "\r\n\r\n");
  return n;
}

// Закрывающая граница "--<boundary>--\r\n"
inline size_t closingSize(const char* boundary) {
  return 2 + cstrLen(boundary) + 4;
}
inline size_t writeClosing(uint8_t* out, const char* boundary) {
  size_t n = 0;
  n += writeRaw(out + n, "--");
  n += writeRaw(out + n, boundary);
  n += writeRaw(out + n, "--\r\n");
  return n;
}

// Полный размер тела для запроса к Groq: файл + три поля + закрытие.
// Между телом файла и следующей границей стоит CRLF.
inline size_t totalSize(const char* boundary, size_t dataBytes) {
  return fileHeaderSize(boundary, "file", "audio.wav", "audio/wav") +
         dataBytes + 2 +
         fieldSize(boundary, "model", "whisper-large-v3-turbo") +
         fieldSize(boundary, "response_format", "json") +
         fieldSize(boundary, "language", "ru") +
         closingSize(boundary);
}

}  // namespace mp
