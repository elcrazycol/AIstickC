# Милестоун 1: распознавание речи — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Запись с микрофона уходит в Groq Whisper и возвращается текстом, который виден на экране.

**Architecture:** Тело multipart/form-data генерируется потоково через подкласс `Stream` — Arduino-овский `HTTPClient::sendRequest(type, Stream*, size)` читает из него и пишет в сокет. Второй буфер на 160 КБ не нужен. Раскладка тела (границы, заголовки частей, суммарная длина) вынесена в чистый модуль и тестируется на Mac.

**Tech Stack:** PlatformIO, Arduino ESP32, M5Unified, `HTTPClient` + `WiFiClientSecure`, pyserial.

---

## Контекст для исполнителя

Проект: `/Users/lesha/codes/aistick`, милестоун 0 уже сделан и проверен на
железе (см. `docs/superpowers/specs/2026-10-11-voice-assistant-design.md`).

Что известно из милестоуна 0 и обязательно к учёту:

- **`pio` из Bash-инструмента не работает** — песочница блокирует
  `~/.platformio`. Сборка и прошивка только через `run_in_terminal`.
- **Тесты — только `tools/run_tests.sh`** (clang++, без PlatformIO, без сети).
  Unity не используется.
- **`M5.Mic.record(buf, len, rate)` — длина в сэмплах**, не в байтах.
- **Прогрев 200 мс обязателен** — иначе DC-ступенька в начале записи.
- **`pio device monitor` не работает через пайп** — только `capture.py`.
- **DMA не пишет в PSRAM** — захват в DRAM, `memcpy` в PSRAM.

Проверенные на железе значения: PSRAM 2 095 103 байт, запись 5 с = 160 000
байт за 4953 мс, речь пишется с пиком −18 dBFS.

Groq API (проверено по докам):

- URL `https://api.groq.com/openai/v1/audio/transcriptions`
- `file` (WAV), `model=whisper-large-v3-turbo`, `response_format=json`,
  `language=ru`
- Ответ: `{"text": "..."}`
- Лимит файла 25 МБ на бесплатном тарифе — наши 160 КБ сильно меньше.
- Минимальная длительность 0.01 с.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `src/pure/multipart.h` | ЧИСТЫЙ: раскладка тела multipart, байт-в-байт |
| `src/pure/json_extract.h` | ЧИСТЫЙ: вытащить `text` из ответа Groq |
| `src/multipart_stream.h` | Адаптер `Stream`, отдаёт тело по кускам |
| `src/net.cpp/.h` | Wi-Fi + TLS-клиент |
| `src/stt.cpp/.h` | Запрос к Groq, разбор ответа |
| `include/settings.h` | Дополнить: endpoint, модель, язык, таймауты |
| `src/main.cpp` | Прошивка: кнопки, состояния, запись, STT, вывод |
| `test/test_multipart.cpp` | Тесты раскладки multipart |
| `test/test_json.cpp` | Тесты разбора ответа |

---

### Task 1: Чистый модуль multipart

**Files:**
- Create: `src/pure/multipart.h`
- Test: `test/test_multipart.cpp`

- [ ] **Step 1: Написать провальный тест**

Создать `test/test_multipart.cpp`:

```cpp
#include "pure_test.h"
#include "multipart.h"

#include <cstring>
#include <string>
#include <vector>

// Собирает тело целиком через чистые функции — то же, что делает
// MultipartStream, но без Arduino.
static std::string buildBody(const char* boundary, const uint8_t* data,
                             size_t dataLen) {
  std::vector<uint8_t> out(mp::totalSize(boundary, dataLen));
  size_t off = 0;
  off += mp::writeFileHeader(out.data() + off, boundary, "file", "audio.wav",
                             "audio/wav");
  std::memcpy(out.data() + off, data, dataLen);
  off += dataLen;
  off += mp::writeField(out.data() + off, boundary, "model",
                        "whisper-large-v3-turbo");
  off += mp::writeField(out.data() + off, boundary, "response_format", "json");
  off += mp::writeField(out.data() + off, boundary, "language", "ru");
  off += mp::writeClosing(out.data() + off, boundary);
  return std::string((const char*)out.data(), off);
}

static void test_total_size_matches_written() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK_EQ(body.size(), mp::totalSize(b, 5));
}

static void test_opens_with_boundary() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK_EQ(body.compare(0, 2, "--"), 0);
  CHECK(body.find(std::string("--") + b + "\r\n") == 0);
}

static void test_file_part_headers() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK(body.find("Content-Disposition: form-data; name=\"file\"; "
                  "filename=\"audio.wav\"\r\n") != std::string::npos);
  CHECK(body.find("Content-Type: audio/wav\r\n\r\n") != std::string::npos);
}

static void test_audio_bytes_verbatim() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = {0x00, 0xFF, 0x7F, 0x80, 0x0A, 0x0D};
  std::string body = buildBody(b, payload, sizeof(payload));
  // Байты не должны быть экранированы или изменены.
  CHECK(body.find(std::string((const char*)payload, sizeof(payload))) !=
        std::string::npos);
}

static void test_fields_present() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  CHECK(body.find("name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n") !=
        std::string::npos);
  CHECK(body.find("name=\"response_format\"\r\n\r\njson\r\n") !=
        std::string::npos);
  CHECK(body.find("name=\"language\"\r\n\r\nru\r\n") != std::string::npos);
}

static void test_closing_boundary_has_dashes() {
  const char* b = "----aistickBoundary";
  const uint8_t payload[] = "AUDIO";
  std::string body = buildBody(b, payload, 5);
  std::string closing = std::string("--") + b + "--\r\n";
  CHECK(body.size() >= closing.size());
  CHECK_EQ(body.compare(body.size() - closing.size(), closing.size(), closing),
           0);
}

static void test_empty_audio_still_valid() {
  const char* b = "----aistickBoundary";
  std::string body = buildBody(b, (const uint8_t*)"", 0);
  CHECK_EQ(body.size(), mp::totalSize(b, 0));
  CHECK(body.find("--") == 0);
  CHECK(body.size() > 44);
}

int main() {
  test_total_size_matches_written();
  test_opens_with_boundary();
  test_file_part_headers();
  test_audio_bytes_verbatim();
  test_fields_present();
  test_closing_boundary_has_dashes();
  test_empty_audio_still_valid();
  return puretest::summary("multipart");
}
```

- [ ] **Step 2: Прогнать, убедиться что не собирается**

Run:
```bash
tools/run_tests.sh
```
Expected: `СБОРКА ПРОВАЛЕНА: test_multipart` (нет `multipart.h`).

- [ ] **Step 3: Написать реализацию**

Создать `src/pure/multipart.h`:

```cpp
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
```

- [ ] **Step 4: Прогнать тесты**

Run:
```bash
tools/run_tests.sh
```
Expected: `PASS multipart` и все прочие тоже `PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/pure/multipart.h test/test_multipart.cpp
git commit -m "feat: раскладка multipart/form-data с тестами"
```

---

### Task 2: Чистый модуль разбора ответа

**Files:**
- Create: `src/pure/json_extract.h`
- Test: `test/test_json.cpp`

- [ ] **Step 1: Написать провальный тест**

Создать `test/test_json.cpp`:

```cpp
#include "pure_test.h"
#include "json_extract.h"

#include <string>

static void test_simple_text() {
  std::string body = R"({"text":"привет мир"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "привет мир");
}

static void test_text_with_spaces_around_colon() {
  std::string body = R"({ "text" : "пробелы" })";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "пробелы");
}

static void test_text_after_other_fields() {
  // У Groq поле text может идти не первым.
  std::string body =
      R"({"task":"transcribe","language":"ru","duration":5.0,"text":"готово"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "готово");
}

static void test_escaped_quotes() {
  std::string body = R"({"text":"он сказал \"да\""})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "он сказал \"да\"");
}

static void test_escaped_backslash_and_newline() {
  std::string body = R"({"text":"a\\b\nc"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "a\\b\nc");
}

static void test_missing_key_fails() {
  std::string body = R"({"error":"bad key"})";
  std::string out;
  CHECK(!jsonx::extractString(body.c_str(), "text", out));
}

static void test_truncated_json_fails() {
  // Ответ обрезан — не должно быть мусора на выходе.
  std::string body = R"({"text":"оборвано)";
  std::string out;
  CHECK(!jsonx::extractString(body.c_str(), "text", out));
}

static void test_empty_text_is_valid() {
  std::string body = R"({"text":""})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "");
}

static void test_key_inside_value_not_matched() {
  // Слово text встречается в значении другого поля — не путать с ключом.
  std::string body = R"({"prompt":"some text here","text":"верное"})";
  std::string out;
  CHECK(jsonx::extractString(body.c_str(), "text", out));
  CHECK_STR_EQ(out, "верное");
}

int main() {
  test_simple_text();
  test_text_with_spaces_around_colon();
  test_text_after_other_fields();
  test_escaped_quotes();
  test_escaped_backslash_and_newline();
  test_missing_key_fails();
  test_truncated_json_fails();
  test_empty_text_is_valid();
  test_key_inside_value_not_matched();
  return puretest::summary("json");
}
```

- [ ] **Step 2: Прогнать, убедиться что не собирается**

Run:
```bash
tools/run_tests.sh
```
Expected: `СБОРКА ПРОВАЛЕНА: test_json`.

- [ ] **Step 3: Написать реализацию**

Создать `src/pure/json_extract.h`:

```cpp
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
    const char close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (s[i] != 0) {
      if (s[i] == '"') {
        std::string tmp;
        if (!readString(s, i, tmp)) return false;
        continue;
      }
      if (s[i] == c) ++depth;
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
```

- [ ] **Step 4: Прогнать тесты**

Run:
```bash
tools/run_tests.sh
```
Expected: `PASS json` и остальные тоже.

- [ ] **Step 5: Commit**

```bash
git add src/pure/json_extract.h test/test_json.cpp
git commit -m "feat: разбор JSON-ответа с тестами"
```

---

### Task 3: Адаптер Stream для тела multipart

**Files:**
- Create: `src/multipart_stream.h`

Этот файл зависит от Arduino и потому не тестируется на Mac. Его логика —
чистый обход раскладки из Task 1, который протестирован.

- [ ] **Step 1: Написать адаптер**

Создать `src/multipart_stream.h`:

```cpp
// Отдаёт тело multipart/form-data как Arduino Stream, не собирая его
// целиком в памяти. HTTPClient::sendRequest(type, Stream*, size) читает
// отсюда и пишет прямо в сокет.
//
// Тело состоит из отрезков:
//   [заголовок файловой части][байты WAV из PSRAM][\r\n]
//   [поле model][поле response_format][поле language][закрытие]
//
// Позиция внутри тела вычисляется по смещению, поэтому random access
// не нужен — достаточно последовательного чтения.
#pragma once

#include <Arduino.h>
#include <cstring>
#include "pure/multipart.h"

class MultipartStream : public Stream {
 public:
  MultipartStream(const char* boundary, const uint8_t* wav, size_t wavLen)
      : boundary_(boundary), wav_(wav), wavLen_(wavLen) {
    // Считаем границы отрезков один раз.
    fileHeaderLen_ = mp::fileHeaderSize(boundary_, "file", "audio.wav",
                                        "audio/wav");
    modelLen_  = mp::fieldSize(boundary_, "model", "whisper-large-v3-turbo");
    formatLen_ = mp::fieldSize(boundary_, "response_format", "json");
    langLen_   = mp::fieldSize(boundary_, "language", "ru");
    closeLen_  = mp::closingSize(boundary_);

    total_ = mp::totalSize(boundary_, wavLen_);
    pos_ = 0;
    scratch_ = nullptr;
  }

  int available() override {
    return (int)(total_ - pos_);
  }

  int read() override {
    uint8_t b;
    return readBytes(&b, 1) == 1 ? (int)b : -1;
  }

  int peek() override {
    // Для нашего потока peek не нужен HTTPClient'ом; возвращаем
    // текущий байт, не сдвигая позицию.
    if (pos_ >= total_) return -1;
    uint8_t b;
    const size_t save = pos_;
    readBytes(&b, 1);
    pos_ = save;
    return (int)b;
  }

  size_t readBytes(uint8_t* buffer, size_t length) override {
    size_t written = 0;
    while (written < length && pos_ < total_) {
      // Определяем, в каком отрезке мы находимся.
      if (pos_ < fileHeaderLen_) {
        written += emit(&buffer[written], length - written, 0, fileHeaderLen_,
                        &MultipartStream::writeFileHeaderAt);
        continue;
      }
      const size_t fileStart = fileHeaderLen_;
      if (pos_ < fileStart + wavLen_) {
        const size_t off = pos_ - fileStart;
        const size_t n = min(length - written, wavLen_ - off);
        memcpy(&buffer[written], wav_ + off, n);
        written += n;
        pos_ += n;
        continue;
      }
      const size_t crlfStart = fileStart + wavLen_;
      if (pos_ < crlfStart + 2) {
        const size_t off = pos_ - crlfStart;
        const char* crlf = "\r\n";
        const size_t n = min(length - written, (size_t)2 - off);
        memcpy(&buffer[written], crlf + off, n);
        written += n;
        pos_ += n;
        continue;
      }
      const size_t modelStart = crlfStart + 2;
      if (pos_ < modelStart + modelLen_) {
        written += emit(&buffer[written], length - written, modelStart,
                        modelLen_, &MultipartStream::writeModelAt);
        continue;
      }
      const size_t formatStart = modelStart + modelLen_;
      if (pos_ < formatStart + formatLen_) {
        written += emit(&buffer[written], length - written, formatStart,
                        formatLen_, &MultipartStream::writeFormatAt);
        continue;
      }
      const size_t langStart = formatStart + formatLen_;
      if (pos_ < langStart + langLen_) {
        written += emit(&buffer[written], length - written, langStart,
                        langLen_, &MultipartStream::writeLangAt);
        continue;
      }
      const size_t closeStart = langStart + langLen_;
      written += emit(&buffer[written], length - written, closeStart,
                      closeLen_, &MultipartStream::writeCloseAt);
    }
    return written;
  }

  size_t write(uint8_t) override { return 0; }  // только чтение
  size_t write(const uint8_t*, size_t) override { return 0; }

  size_t totalSize() const { return total_; }

 private:
  typedef size_t (MultipartStream::*Writer)(uint8_t*, const char*);

  // Общий помощник: генерируем отрезок в scratch и копируем нужный кусок.
  size_t emit(uint8_t* dst, size_t room, size_t segStart, size_t segLen,
              Writer fn) {
    const size_t off = pos_ - segStart;
    const size_t n = min(room, segLen - off);
    ensureScratch(segLen);
    if (scratch_ == nullptr) return 0;
    (this->*fn)(scratch_, boundary_);
    memcpy(dst, scratch_ + off, n);
    pos_ += n;
    return n;
  }

  void ensureScratch(size_t need) {
    if (scratch_ != nullptr && scratchLen_ >= need) return;
    free(scratch_);
    scratch_ = (uint8_t*)malloc(need);
    scratchLen_ = (scratch_ != nullptr) ? need : 0;
  }

  size_t writeFileHeaderAt(uint8_t* o, const char* b) {
    return mp::writeFileHeader(o, b, "file", "audio.wav", "audio/wav");
  }
  size_t writeModelAt(uint8_t* o, const char* b) {
    return mp::writeField(o, b, "model", "whisper-large-v3-turbo");
  }
  size_t writeFormatAt(uint8_t* o, const char* b) {
    return mp::writeField(o, b, "response_format", "json");
  }
  size_t writeLangAt(uint8_t* o, const char* b) {
    return mp::writeField(o, b, "language", "ru");
  }
  size_t writeCloseAt(uint8_t* o, const char* b) {
    return mp::writeClosing(o, b);
  }

  const char* boundary_;
  const uint8_t* wav_;
  size_t wavLen_;

  size_t fileHeaderLen_, modelLen_, formatLen_, langLen_, closeLen_;
  size_t total_;
  size_t pos_;

  uint8_t* scratch_ = nullptr;
  size_t scratchLen_ = 0;
};
```

- [ ] **Step 2: Проверить, что заголовок хотя бы разбирается компилятором**

Заголовок зависит от Arduino, поэтому на Mac не соберётся. Проверка
синтаксиса — только на этапе сборки прошивки (Task 6).

- [ ] **Step 3: Commit**

```bash
git add src/multipart_stream.h
git commit -m "feat: Stream-адаптер для потоковой отправки multipart"
```

---

### Task 4: Настройки и сеть

**Files:**
- Modify: `include/settings.h`
- Create: `src/net.h`
- Create: `src/net.cpp`

- [ ] **Step 1: Дополнить `include/settings.h`**

Добавить в конец файла:

```cpp
// --- STT (Groq) ---
constexpr const char* kGroqHost = "api.groq.com";
constexpr int         kGroqPort = 443;
constexpr const char* kGroqPath = "/openai/v1/audio/transcriptions";
constexpr const char* kSttModel = "whisper-large-v3-turbo";
constexpr const char* kSttLang  = "ru";
constexpr const char* kSttBoundary = "----aistickBoundary7f3a";

// --- Таймауты сети, мс ---
constexpr uint32_t kWifiTimeoutMs    = 15000;
constexpr uint32_t kHttpConnectMs    = 5000;
constexpr uint32_t kHttpReadMs       = 15000;
constexpr size_t   kMaxSttResponse   = 8192;   // больше ответ не бывает
```

- [ ] **Step 2: Написать `src/net.h`**

```cpp
// Подключение к Wi-Fi и общий TLS-клиент.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

namespace net {

// Подключается к Wi-Fi. Возвращает true при успехе.
// При неудаче текст причины доступен в lastError().
bool connectWifi(uint32_t timeoutMs);

// Последняя ошибка Wi-Fi или сети.
const char* lastError();

// Общий TLS-клиент. setInsecure: без пиннинга сертификата.
// Для v1 принято осознанно (см. спеку, раздел 7).
WiFiClientSecure& tlsClient();

}  // namespace net
```

- [ ] **Step 3: Написать `src/net.cpp`**

```cpp
#include "net.h"

#include "config.h"
#include "settings.h"

namespace net {

namespace {
char g_error[96] = {0};
WiFiClientSecure g_client;
bool g_clientReady = false;
}  // namespace

const char* lastError() { return g_error; }

WiFiClientSecure& tlsClient() {
  if (!g_clientReady) {
    // Без пиннинга сертификата: просто и работает, но MITM-незащищено.
    g_client.setInsecure();
    g_client.setTimeout(kHttpReadMs);
    g_clientReady = true;
  }
  return g_client;
}

bool connectWifi(uint32_t timeoutMs) {
  const char* ssid = WIFI_SSID;
  if (ssid == nullptr || ssid[0] == 0) {
    snprintf(g_error, sizeof(g_error), "SSID пуст — заполни config.h");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, WIFI_PASS);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - started > timeoutMs) {
      snprintf(g_error, sizeof(g_error), "Wi-Fi: таймаут (%d)",
               (int)WiFi.status());
      WiFi.disconnect(true);
      return false;
    }
    delay(100);
  }

  snprintf(g_error, sizeof(g_error), "ok");
  return true;
}

}  // namespace net
```

- [ ] **Step 4: Проверить сборку**

Run (в терминале, не в Bash-инструменте):
```bash
pio run -e esp32dev
```
Expected: `SUCCESS`. На этом шаге точкой входа остаётся `probe_main.cpp`
(в `build_src_filter` исключён `main.cpp`), а `net.cpp` просто компилируется
и линкуется — проверить его работу можно будет только в Task 6.

- [ ] **Step 5: Commit**

```bash
git add include/settings.h src/net.h src/net.cpp
git commit -m "feat: подключение к Wi-Fi и TLS-клиент"
```

---

### Task 5: Модуль STT

**Files:**
- Create: `src/stt.h`
- Create: `src/stt.cpp`

- [ ] **Step 1: Написать `src/stt.h`**

```cpp
// Отправка WAV в Groq Whisper и разбор ответа.
#pragma once

#include <Arduino.h>

namespace stt {

struct Result {
  bool ok = false;
  String text;      // распознанный текст при ok == true
  String error;     // описание при ok == false
  int httpCode = 0; // код ответа, если дошло до него
};

// wav — буфер с WAV целиком (заголовок + аудио) в PSRAM.
Result transcribe(const uint8_t* wav, size_t wavLen);

}  // namespace stt
```

- [ ] **Step 2: Написать `src/stt.cpp`**

```cpp
#include "stt.h"

#include <HTTPClient.h>

#include "config.h"
#include "settings.h"
#include "multipart_stream.h"
#include "net.h"
#include "pure/json_extract.h"

namespace stt {

Result transcribe(const uint8_t* wav, size_t wavLen) {
  Result r;

  if (wav == nullptr || wavLen == 0) {
    r.error = "пустой буфер";
    return r;
  }

  HTTPClient http;
  http.setConnectTimeout(kHttpConnectMs);
  http.setTimeout(kHttpReadMs);
  http.setReuse(false);

  if (!http.begin(net::tlsClient(), kGroqHost, kGroqPort, kGroqPath, true)) {
    r.error = "не открыть соединение";
    return r;
  }

  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  http.addHeader("Content-Type",
                 String("multipart/form-data; boundary=") + kSttBoundary);

  MultipartStream body(kSttBoundary, wav, wavLen);
  const size_t bodyLen = body.totalSize();

  Serial.printf("[stt] отправляю %u байт тела\n", (unsigned)bodyLen);
  const uint32_t t0 = millis();
  r.httpCode = http.sendRequest("POST", &body, bodyLen);
  Serial.printf("[stt] ответ %d за %u мс\n", r.httpCode, millis() - t0);

  if (r.httpCode <= 0) {
    r.error = String("сеть: ") + http.errorToString(r.httpCode);
    http.end();
    return r;
  }

  String payload = http.getString();
  http.end();

  if (r.httpCode != 200) {
    // Показываем начало тела: там причина отказа.
    String head = payload.substring(0, 60);
    r.error = String("HTTP ") + r.httpCode + ": " + head;
    return r;
  }

  std::string out;
  if (!jsonx::extractString(payload.c_str(), "text", out)) {
    r.error = String("не разобран ответ: ") + payload.substring(0, 60);
    return r;
  }

  r.text = String(out.c_str());
  r.ok = true;
  return r;
}

}  // namespace stt
```

- [ ] **Step 3: Commit**

```bash
git add src/stt.h src/stt.cpp
git commit -m "feat: запрос к Groq Whisper"
```

---

### Task 6: Прошивка милестоуна 1

**Files:**
- Create: `src/main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Написать `src/main.cpp`**

```cpp
// Милестоун 1: запись → Groq Whisper → текст на экране.
//
// Управление:
//   BtnA (зажать) — запись, отпустить — отправить
//   BtnB (тап)    — очистить экран
//
// Шрифт пока встроенный: красивый моноширинный с кириллицей — милестоун 5.

#include <Arduino.h>
#include <M5Unified.h>

#include "config.h"
#include "settings.h"
#include "net.h"
#include "stt.h"
#include "pure/wav.h"

namespace {

// Константы цвета живут в lgfx::colors и НЕ подтягиваются в глобальную
// область через M5GFX.h — проверено на минимальном примере. Пишем полное
// имя, иначе сборка падает на «use of undeclared identifier 'TFT_BLACK'».
constexpr int kBlack = lgfx::colors::TFT_BLACK;
constexpr int kRed   = lgfx::colors::TFT_RED;
constexpr int kGreen = lgfx::colors::TFT_GREEN;
constexpr int kYellow= lgfx::colors::TFT_YELLOW;
constexpr int kWhite = lgfx::colors::TFT_WHITE;

uint8_t* g_audio = nullptr;   // PSRAM: 44 байта заголовка + аудио
int16_t  g_chunk[kDmaChunkBytes / 2];

enum class State { Idle, Recording, Sending, Showing };
State g_state = State::Idle;
String g_shown;

void showStatus(const char* text, uint16_t color) {
  M5.Display.fillScreen(kBlack);
  M5.Display.setTextColor(color, kBlack);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 4);
  M5.Display.print(text);
}

// ВНИМАНИЕ: LGFXBase объявляет `using Print::write`, но не `using
// Print::print`. Поэтому print(const String&) скрыт, и String сюда
// передавать нельзя. Печатаем через c_str().
void showWrapped(const char* text, uint16_t color) {
  M5.Display.fillScreen(kBlack);
  M5.Display.setTextColor(color, kBlack);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 4);
  M5.Display.print(text);   // перенос по словам добавим в милестоуне 5
}

// Захват audioBytes байт в PSRAM. Возвращает число записанных байт.
size_t record(size_t audioBytes) {
  const size_t totalSamples = audioBytes / 2;

  // Прогрев: первые 200 мс — переходный процесс PDM, не звук.
  {
    const size_t warm = (size_t)kSampleRate * 200 / 1000;
    size_t got = 0;
    while (got < warm) {
      const size_t want = min(warm - got, sizeof(g_chunk) / 2);
      if (!M5.Mic.record(g_chunk, want, kSampleRate)) { delay(1); continue; }
      while (M5.Mic.isRecording()) delay(1);
      got += want;
    }
  }

  size_t done = 0;
  const uint32_t started = millis();
  while (done < totalSamples &&
         (millis() - started) < (kMaxRecordMs + 2000)) {
    const size_t want = min(totalSamples - done, sizeof(g_chunk) / 2);
    if (!M5.Mic.record(g_chunk, want, kSampleRate)) { delay(1); continue; }
    while (M5.Mic.isRecording()) delay(1);
    memcpy(g_audio + wav::headerSize() + done * 2, g_chunk, want * 2);
    done += want;

    // Кнопка отпущена — конец записи.
    M5.update();
    if (!M5.BtnA.isPressed()) break;
  }
  return done * 2;
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(kScreenRotation);
  M5.Display.setTextWrap(true);

  const size_t total = wav::totalSize(kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    showStatus("Нет PSRAM", kRed);
    Serial.println("[main] ОШИБКА: PSRAM недоступна");
    while (true) delay(1000);
  }

  if (!M5.Mic.begin()) {
    showStatus("Микрофон не стартовал", kRed);
    while (true) delay(1000);
  }

  showStatus("Wi-Fi...", kYellow);
  if (!net::connectWifi(kWifiTimeoutMs)) {
    showStatus(net::lastError(), kRed);
    Serial.printf("[main] %s\n", net::lastError());
    // Не блокируемся: BtnA попробует подключиться снова.
    return;
  }
  Serial.printf("[main] Wi-Fi ок, IP %s\n", WiFi.localIP().toString().c_str());
  showStatus("Ready. Держи A", kGreen);
}

void loop() {
  M5.update();

  if (M5.BtnB.wasPressed()) {
    g_shown = "";
    g_state = State::Idle;
    showStatus("Ready. Держи A", kGreen);
  }

  if (g_state == State::Idle && M5.BtnA.wasPressed()) {
    if (WiFi.status() != WL_CONNECTED) {
      showStatus("Wi-Fi...", kYellow);
      if (!net::connectWifi(kWifiTimeoutMs)) {
        showStatus(net::lastError(), kRed);
        delay(2000);
        return;
      }
    }

    g_state = State::Recording;
    showStatus("RECORDING...", kRed);
    M5.Speaker.tone(2000, 100);

    const size_t got = record(kAudioBytes);
    const uint32_t ms = got / 2 * 1000 / kSampleRate;
    Serial.printf("[main] записано %u байт (%u мс)\n", (unsigned)got, ms);

    if (got < (size_t)kSampleRate * 2 * kMinRecordMs / 1000) {
      showStatus("Слишком коротко", kYellow);
      delay(1500);
      g_state = State::Idle;
      showStatus("Ready. Держи A", kGreen);
      return;
    }

    wav::writeHeader(g_audio, (uint32_t)got, kSampleRate);

    g_state = State::Sending;
    showStatus("Sending...", kYellow);

    stt::Result r = stt::transcribe(g_audio, wav::totalSize(got));
    if (r.ok) {
      g_shown = r.text;
      showWrapped(g_shown.c_str(), kWhite);
      Serial.printf("[main] текст: %s\n", g_shown.c_str());
    } else {
      g_shown = r.error;
      showWrapped(g_shown.c_str(), kRed);
      Serial.printf("[main] ошибка: %s\n", g_shown.c_str());
    }
    g_state = State::Showing;
  }

  delay(20);
}
```

- [ ] **Step 2: Поправить `platformio.ini`**

`probe_main.cpp` больше не должен собираться: точка входа теперь `main.cpp`.
Исключить его и снять исключение `main.cpp`:

```ini
build_src_filter = +<*> -<probe_main.cpp>
```

Убедиться, что строка `-<main.cpp>` из конфига убрана.

- [ ] **Step 3: Собрать**

Run (в терминале):
```bash
pio run -e esp32dev
```
Expected: `SUCCESS`. Если `MultipartStream` не собрался — смотреть
предупреждения о нереализованных чистых виртуальных методах `Stream`.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp platformio.ini
git commit -m "feat: прошивка милестоуна 1 — запись и распознавание"
```

---

### Task 7: Проверка на живом устройстве

**Files:** нет изменений — только проверка.

- [ ] **Step 1: Пользователь заполняет `include/config.h`**

Нужны `GROQ_API_KEY`, `WIFI_SSID`, `WIFI_PASS`. `OPENCODE_API_KEY` пока не
нужен — он для милестоуна 2.

- [ ] **Step 2: Прошить**

```bash
pio run -e esp32dev -t upload
```

- [ ] **Step 3: Смотреть Serial**

```bash
python3 tools/capture.py /dev/null
```
Скрипт печатает весь лог в stderr; на экране устройства должен появиться
`Ready. Держи A` зелёным.

- [ ] **Step 4: Проверить распознавание**

Зажать BtnA, сказать по-русски фразу, отпустить. Ожидается:

```
[main] записано 96000 байт (3000 мс)
[stt] отправляю 96424 байт тела
[stt] ответ 200 за 1200 мс
[main] текст: <распознанная фраза>
```

На экране — распознанный текст белым.

- [ ] **Step 5: Разбор отказов**

| Что видно | Диагноз | Действие |
|---|---|---|
| `Wi-Fi: таймаут` | Сеть 5 ГГц или неверный пароль | Проверить, что точка 2.4 ГГц |
| `HTTP 401` | Ключ Groq неверен | Перевыпустить на console.groq.com |
| `HTTP 413` | Файл велик | Уменьшить `kMaxRecordMs` |
| `HTTP 429` | Лимит запросов | Подождать; бесплатный тариф ограничен |
| `сеть: connection refused` | TLS не поднялся | Проверить `setInsecure()` и `kGroqPort` |
| `не разобран ответ` | Формат ответа иной | Смотреть тело в Serial |
| `не расслышал` | Тишина в записи | Говорить громче, ближе к микрофону |
| Мусор вместо текста | Multipart собран неверно | Сверить `totalSize` с отправленным |

---

## Самопроверка плана

**Покрытие спеки.** Раздел 7 (STT → Groq, потоковая отправка): Tasks 1, 3, 5.
Раздел 7 (TLS `setInsecure`): Task 4. Раздел 9 (таблица ошибок): Task 6, 7.
Раздел 2 п. 2 (DMA/PSRAM): Task 6 `record()`. Раздел 6 (прогрев): Task 6.
Раздел 8 (шрифт, перенос): намеренно вне — милестоун 5, здесь встроенный
шрифт и `setTextWrap`.

**Плейсхолдеры.** Не осталось: каждый шаг с кодом содержит полный код.

**Согласованность типов.** `mp::totalSize/writeField/writeFileHeader/
writeClosing/fileHeaderSize/fieldSize/closingSize` — Task 1 и Task 3.
`jsonx::extractString` — Task 2 и Task 5. `stt::Result` с полями
`ok/text/error/httpCode` — Task 5 и Task 6. `net::connectWifi/lastError/
tlsClient` — Task 4, 5, 6. `MultipartStream(boundary, wav, wavLen)` и
`totalSize()` — Task 3 и Task 5.

**Известный риск.** `MultipartStream::emit` генерирует отрезок в scratch при
каждом вызове — это лишние аллокации, но они мелкие (до ~200 байт) и
переиспользуют один буфер. Если окажется медленно, заменить на прямую запись
в `dst` без scratch.
