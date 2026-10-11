# Милестоун 0: проверка железа — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Получить на Mac файл `test.wav` с реальной записью с микрофона M5StickC PLUS2, чтобы доказать три допущения до написания остальной прошивки: PSRAM включается, PDM-микрофон работает, параметры захвата корректны.

**Architecture:** Прошивка на `esp32dev` пишет 3 секунды с `M5.Mic` и стримит запись по Serial в base64. Скрипт на Mac склеивает base64 обратно в WAV. Плюс скелет проекта (два окружения, дефолты, самодельные тесты без Unity), который понадобится всем последующим милестоунам.

**Tech Stack:** PlatformIO, Arduino ESP32 (framework 3.20017), M5Unified, Python 3 (только stdlib — `tools/capture.py`), clang++ (тесты).

---

## Контекст для исполнителя

Ты работаешь в `/Users/lesha/codes/aistick`. Репозиторий пока не
инициализирован — это делается в Task 2, потому что песочница запрещает
создавать `.git` (см. раздел 11 спеки). Если `git init` не проходит — это
ожидаемо, пропусти шаг коммита и отметь его в отчёте.

Ключевые факты о железе (подробно в спеке, раздел 2):
- ESP32 классический, PSRAM 2 МБ, включается флагом `-DBOARD_HAS_PSRAM`.
- Микрофон SPM1423, PDM, пины DATA=GPIO34, CLK=GPIO0. M5Unified настраивает
  это сама; вручную PDM включать не надо.
- DMA не работает с PSRAM. Захват — в DRAM, копирование в PSRAM.
- Динамик — пьезо-бузер на GPIO2 (PWM), с микрофоном не конфликтует.

PlatformIO Core **не установлен** на машине. Сборку и прошивку выполняет
пользователь. Ты пишешь код, генерируешь шрифт и прогоняешь тесты чистой
логики через `tools/run_tests.sh` (работает офлайн, без PlatformIO).

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `platformio.ini` | Два окружения: `esp32dev` (прошивка), `native` (для IDE) |
| `include/settings.h` | Несекретные настройки: пины, частота, лимиты, повороты |
| `include/config.h.example` | Шаблон секретов, коммитится |
| `include/config.h` | Реальные ключи, в `.gitignore` |
| `include/probe.h` | Общие определения милестоуна 0 (частота, размер буфера) |
| `src/probe_main.cpp` | Прошивка милестоуна 0 |
| `src/pure/wav.h` | Сборка/завершение WAV-заголовка (чистый модуль) |
| `src/pure/cyrillic.h` | UTF-8: декод кодпоинтов, ширина строки |
| `src/pure/base64.h` | Кодирование/декодирование base64 (чистый модуль) |
| `test/pure_test.h` | Обвязка CHECK/CHECK_EQ без Unity |
| `test/test_wav.cpp` | Тесты WAV-заголовка |
| `test/test_cyrillic.cpp` | Тесты UTF-8 и ширины |
| `test/test_base64.cpp` | Тесты base64 |
| `tools/run_tests.sh` | Компиляция и запуск всех тестов |
| `tools/capture.py` | base64 со Serial → `test.wav` |
| `README.md` | Как собрать, прошить, слушать |
| `.gitignore` | `.pio/`, `.vscode/`, `include/config.h`, `test.wav` |

---

### Task 1: Тестовая обвязка и скрипт прогона

Без Unity: он тянется из реестра PlatformIO, а сети нет. Своя обвязка на 40
строк работает офлайн и запускается прямо в песочнице.

**Files:**
- Create: `test/pure_test.h`
- Create: `tools/run_tests.sh`

- [ ] **Step 1: Написать обвязку**

Создать `test/pure_test.h`:

```cpp
// Крошечная тестовая обвязка. Заменяет Unity, который требует сети.
#pragma once
#include <cstdio>
#include <cmath>
#include <string>

namespace puretest {

inline int& failures() { static int n = 0; return n; }
inline int& checks()   { static int n = 0; return n; }

inline void report(const char* file, int line, const char* expr, bool ok) {
  ++checks();
  if (!ok) {
    ++failures();
    std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
  }
}

inline int summary(const char* suite) {
  if (failures() == 0) {
    std::printf("PASS %s (%d проверок)\n", suite, checks());
    return 0;
  }
  std::printf("FAIL %s (%d из %d провалено)\n", suite, failures(), checks());
  return 1;
}

}  // namespace puretest

#define CHECK(cond) ::puretest::report(__FILE__, __LINE__, #cond, (cond))

#define CHECK_EQ(a, b)                                                      \
  do {                                                                      \
    auto _a = (a);                                                          \
    auto _b = (b);                                                          \
    ::puretest::report(__FILE__, __LINE__, #a " == " #b, _a == _b);         \
  } while (0)

#define CHECK_STR_EQ(a, b)                                                  \
  do {                                                                      \
    std::string _a((a));                                                    \
    std::string _b((b));                                                    \
    ::puretest::report(__FILE__, __LINE__, #a " == " #b, _a == _b);         \
  } while (0)
```

- [ ] **Step 2: Написать скрипт прогона**

Создать `tools/run_tests.sh`:

```bash
#!/usr/bin/env bash
# Компилирует и запускает все тесты из test/test_*.cpp обычным clang++.
# Не требует PlatformIO и сети.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-clang++}"
FAILED=0
RAN=0

for src in "$ROOT"/test/test_*.cpp; do
  [ -e "$src" ] || continue
  name="$(basename "$src" .cpp)"
  out="$(mktemp -t "$name")"
  if ! "$CXX" -std=c++17 -Wall -Wextra -I"$ROOT/src/pure" -I"$ROOT/test" \
        -o "$out" "$src"; then
    echo "СБОРКА ПРОВАЛЕНА: $name"
    FAILED=1
    rc=1
  else
    "$out"
    rc=$?
  fi
  RAN=$((RAN + 1))
  [ "$rc" -eq 0 ] || FAILED=1
  rm -f "$out"
done

if [ "$RAN" -eq 0 ]; then
  echo "Тестов не найдено."
  exit 1
fi

if [ "$FAILED" -eq 0 ]; then
  echo "Все тесты прошли ($RAN)."
  exit 0
fi
echo "Есть провалившиеся тесты."
exit 1
```

- [ ] **Step 3: Сделать скрипт исполняемым и проверить поведение без тестов**

Run:
```bash
chmod +x tools/run_tests.sh && tools/run_tests.sh
```
Expected: `Тестов не найдено.` и код возврата 1. Это корректно — тесты
появятся в Task 4.

- [ ] **Step 4: Commit**

```bash
git add test/pure_test.h tools/run_tests.sh
git commit -m "test: обвязка CHECK/CHECK_EQ и прогон без Unity"
```

---

### Task 2: Скелет репозитория

**Files:**
- Create: `.gitignore`
- Create: `platformio.ini`
- Create: `include/settings.h`
- Create: `include/config.h.example`
- Create: `include/config.h`
- Create: `README.md`

- [ ] **Step 1: Инициализировать репозиторий**

Run:
```bash
git init
```
Expected: `Initialized empty Git repository`.

Если команда падает с `Operation not permitted` — песочница запрещает `.git`.
Пропусти все шаги коммита в этом плане и укажи это в отчёте; код всё равно
пиши.

- [ ] **Step 2: Написать `.gitignore`**

```gitignore
# Секреты — никогда в git
include/config.h

# Сборка PlatformIO
.pio/
.pioenvs/
.piolibdeps/

# Редактор
.vscode/
.idea/

# Артефакты милестоуна 0
test.wav
*.wav

# macOS
.DS_Store
```

- [ ] **Step 3: Написать `include/settings.h`**

Несекретные значения. Секреты живут в `config.h` (Step 5).

```cpp
// Настройки, которые не являются секретами и коммитятся.
#pragma once

// --- Аудио ---
constexpr int      kSampleRate      = 16000;   // Гц, моно, 16 бит
constexpr uint32_t kMaxRecordMs     = 8000;    // максимум записи, мс
constexpr uint32_t kMinRecordMs     = 300;     // короче — отбрасываем
constexpr size_t   kDmaChunkBytes   = 8192;    // кусок захвата в DRAM
constexpr size_t   kAudioBytes      = (size_t)kSampleRate * 2 *
                                      (kMaxRecordMs / 1000);
// kAudioBytes = 256000 при 8 с

// --- Кнопки ---
constexpr uint32_t kLongPressMs     = 800;     // порог «долгого» BtnB

// --- Экран ---
constexpr int      kScreenRotation  = 1;       // ландшафт: 1 или 3
constexpr int      kScreenW         = 240;
constexpr int      kScreenH         = 135;

// --- Прочее ---
constexpr uint32_t kSerialBaud      = 115200;
```

- [ ] **Step 4: Написать `platformio.ini`**

```ini
; Милестоун 0: проверка железа.
; Внимание: BOARD_HAS_PSRAM обязателен. Без него ядро arduino-esp32
; выкидывает CONFIG_SPIRAM и не вызывает psramInit() — PSRAM не появится.

[env:esp32dev]
platform = espressif32@6.13.0
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 1500000
upload_port = /dev/cu.usbserial-5A6D0057111
build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
build_src_filter = +<*> -<main.cpp>
lib_deps =
    m5stack/M5Unified@^0.2.7

; Окружение для IDE: компилирует только чистые модули под Mac.
; Тесты запускаются через tools/run_tests.sh и PlatformIO не требуют.
[env:native]
platform = native
build_src_filter = +<pure/>
```

Пояснение к `build_src_filter`: милестоун 0 использует `src/probe_main.cpp`
как точку входа, поэтому `main.cpp` исключён. Остальные модули подключаются
позже и его не требуют.

- [ ] **Step 5: Написать `include/config.h.example` и `include/config.h`**

`include/config.h.example` (коммитится):

```cpp
// Шаблон секретов. Скопируй в config.h и заполни.
// config.h в .gitignore и в репозиторий не попадает.
#pragma once

// Бесплатный ключ с https://console.groq.com/
#define GROQ_API_KEY ""

// Ключ подписки OpenCode Go (https://opencode.ai/go)
#define OPENCODE_API_KEY ""

// Wi-Fi 2.4 ГГц (5 ГГц ESP32 не поддерживает)
#define WIFI_SSID ""
#define WIFI_PASS ""
```

`include/config.h` — та же структура, значения пустые. Создаётся копией:

Run:
```bash
cp include/config.h.example include/config.h
```
Expected: файл создан. `git status` не должен показывать `config.h` (он в
`.gitignore`). Проверь это:

```bash
git status --porcelain include/ | grep -c config.h
```
Expected: `0` — то есть `config.h` не отслеживается.

- [ ] **Step 6: Написать `README.md`**

```markdown
# aistick

Карманный голосовой ассистент на M5StickC PLUS2.
Зажал кнопку → записал → распознал (Groq Whisper) → ответ (DeepSeek через
OpenCode Go) → прочитал на экране.

## Установка инструментов

PlatformIO Core на этой машине не установлен:

    uv tool install platformio

## Секреты

    cp include/config.h.example include/config.h

Заполни `include/config.h`: ключ Groq, ключ OpenCode Go, SSID и пароль Wi-Fi.
Файл в `.gitignore` и в репозиторий не попадает.

## Милестоун 0: проверить микрофон и PSRAM

    pio run -e esp32dev -t upload
    pio device monitor -b 115200 | python3 tools/capture.py test.wav

Потом послушать:

    afplay test.wav

Ожидаемо в Serial:

    [probe] PSRAM найдена: 2097152 байт свободно
    [probe] Запись: 3.0 с
    [probe] Отправлено 32044 байт base64

и в конце — `[probe] Готово: test.wav (96044 байт)`. Длительность около 3 с.

## Тесты чистой логики

Компилируются clang++ и не требуют ни PlatformIO, ни железа:

    tools/run_tests.sh

## Почему так

Подробности в `docs/superpowers/specs/2026-10-11-voice-assistant-design.md`.
Коротко: чип здесь ESP32-PICO-V3-02, а не S3 из исходного ТЗ; микрофон PDM,
а не I2S; PSRAM надо включать флагом `BOARD_HAS_PSRAM`.
```

- [ ] **Step 7: Commit**

```bash
git add .gitignore platformio.ini include/settings.h \
        include/config.h.example README.md
git commit -m "chore: скелет проекта PlatformIO для M5StickC PLUS2"
```

`include/config.h` намеренно не добавляется.

---

### Task 3: Чистый модуль WAV

**Files:**
- Create: `src/pure/wav.h`
- Test: `test/test_wav.cpp`

- [ ] **Step 1: Написать провальный тест**

Создать `test/test_wav.cpp`:

```cpp
#include "pure_test.h"
#include "wav.h"

#include <cstring>

static void test_size_constants() {
  // 44 байта заголовка + данные
  CHECK_EQ(wav::headerSize(), 44u);
  CHECK_EQ(wav::totalSize(1000), 1044u);
  CHECK_EQ(wav::totalSize(0), 44u);
}

static void test_header_bytes_riff() {
  uint8_t buf[44];
  wav::writeHeader(buf, 1000);
  CHECK_EQ(std::memcmp(buf + 0, "RIFF", 4), 0);
  CHECK_EQ(std::memcmp(buf + 8, "WAVE", 4), 0);
  CHECK_EQ(std::memcmp(buf + 12, "fmt ", 4), 0);
  CHECK_EQ(std::memcmp(buf + 36, "data", 4), 0);
}

static void test_header_sizes_little_endian() {
  uint8_t buf[44];
  wav::writeHeader(buf, 1000);

  // ChunkSize = 36 + data, little-endian
  uint32_t chunk = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                   ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
  CHECK_EQ(chunk, 36u + 1000u);

  uint32_t data = (uint32_t)buf[40] | ((uint32_t)buf[41] << 8) |
                  ((uint32_t)buf[42] << 16) | ((uint32_t)buf[43] << 24);
  CHECK_EQ(data, 1000u);
}

static void test_header_pcm_format() {
  uint8_t buf[44];
  wav::writeHeader(buf, 16000);

  auto u16 = [&](int off) {
    return (uint16_t)((uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8));
  };
  auto u32 = [&](int off) {
    return (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
           ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
  };

  CHECK_EQ(u16(20), (uint16_t)1);        // PCM
  CHECK_EQ(u16(22), (uint16_t)1);        // моно
  CHECK_EQ(u32(24), 16000u);             // sample rate
  CHECK_EQ(u32(28), 16000u * 2);         // byte rate = rate * channels * 2
  CHECK_EQ(u16(32), (uint16_t)2);        // block align = channels * 2
  CHECK_EQ(u16(34), (uint16_t)16);       // bits per sample
}

static void test_real_capture_size() {
  // 8 секунд при 16 кГц моно 16 бит = 256000 байт данных
  const uint32_t audio = (uint32_t)16000 * 2 * 8;
  uint8_t buf[44];
  wav::writeHeader(buf, audio);
  auto u32 = [&](int off) {
    return (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
           ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
  };
  CHECK_EQ(audio, 256000u);
  CHECK_EQ(u32(40), 256000u);
  CHECK_EQ(u32(4), 36u + 256000u);
}

int main() {
  test_size_constants();
  test_header_bytes_riff();
  test_header_sizes_little_endian();
  test_header_pcm_format();
  test_real_capture_size();
  return puretest::summary("wav");
}
```

- [ ] **Step 2: Прогнать тест, убедиться что не собирается**

Run:
```bash
tools/run_tests.sh
```
Expected: `СБОРКА ПРОВАЛЕНА: test_wav` — потому что `wav.h` ещё нет.

- [ ] **Step 3: Написать реализацию**

Создать `src/pure/wav.h`:

```cpp
// Сборка WAV-заголовка (RIFF/WAVE, PCM 16 бит).
// Заголовок пишется в начало буфера, аудио идёт с 44-го байта.
#pragma once

#include <cstdint>
#include <cstddef>

namespace wav {

constexpr size_t headerSize() { return 44; }
constexpr size_t totalSize(size_t dataBytes) { return headerSize() + dataBytes; }

inline void putU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline void putU16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void putTag(uint8_t* p, const char* tag) {
  p[0] = (uint8_t)tag[0];
  p[1] = (uint8_t)tag[1];
  p[2] = (uint8_t)tag[2];
  p[3] = (uint8_t)tag[3];
}

// Заголовок для dataBytes байт аудио при sampleRate и 1 канале, 16 бит.
inline void writeHeader(uint8_t* buf, uint32_t dataBytes,
                        uint32_t sampleRate = 16000) {
  const uint16_t channels = 1;
  const uint16_t bits = 16;
  const uint32_t byteRate = sampleRate * channels * (bits / 8);
  const uint16_t blockAlign = (uint16_t)(channels * (bits / 8));

  putTag(buf + 0, "RIFF");
  putU32(buf + 4, 36 + dataBytes);
  putTag(buf + 8, "WAVE");

  putTag(buf + 12, "fmt ");
  putU32(buf + 16, 16);            // размер fmt-блока
  putU16(buf + 20, 1);             // формат PCM
  putU16(buf + 22, channels);
  putU32(buf + 24, sampleRate);
  putU32(buf + 28, byteRate);
  putU16(buf + 32, blockAlign);
  putU16(buf + 34, bits);

  putTag(buf + 36, "data");
  putU32(buf + 40, dataBytes);
}

}  // namespace wav
```

- [ ] **Step 4: Прогнать тесты**

Run:
```bash
tools/run_tests.sh
```
Expected: `PASS wav (N проверок)` и `Все тесты прошли (1).`

- [ ] **Step 5: Commit**

```bash
git add src/pure/wav.h test/test_wav.cpp
git commit -m "feat: модуль WAV-заголовка с тестами"
```

---

### Task 4: Чистый модуль UTF-8 и ширины строк

Милестоун 0 его не использует, но он нужен следующему милестоуну, а сейчас
дёшев — заодно обвязка проверяется на нескольких наборах тестов.

**Files:**
- Create: `src/pure/cyrillic.h`
- Test: `test/test_cyrillic.cpp`

- [ ] **Step 1: Написать провальный тест**

Создать `test/test_cyrillic.cpp`:

```cpp
#include "pure_test.h"
#include "cyrillic.h"

static void test_count_codepoints_ascii() {
  CHECK_EQ(cyr::countCodepoints("hello"), (size_t)5);
  CHECK_EQ(cyr::countCodepoints(""), (size_t)0);
}

static void test_count_codepoints_cyrillic() {
  // «Привет» — 6 букв, каждая по 2 байта в UTF-8
  CHECK_EQ(cyr::countCodepoints("Привет"), (size_t)6);
  CHECK_EQ(cyr::countCodepoints("П"), (size_t)1);
}

static void test_cyrillic_bytes_are_two() {
  const char* s = "Привет";
  CHECK_EQ(cyr::nextCodepoint(s, 0), 2);
}

static void test_decode_codepoint() {
  CHECK_EQ(cyr::codepointAt("A", 0), (uint32_t)'A');
  // Б — U+0411 (кириллическая заглавная Б)
  CHECK_EQ(cyr::codepointAt("\xd0\x91", 0), (uint32_t)0x0411);
  // я — U+044F
  CHECK_EQ(cyr::codepointAt("\xd1\x8f", 0), (uint32_t)0x044F);
}

static void test_next_steps_through_mixed() {
  const char* s = "AБB";  // 1 + 2 + 1 байт
  size_t i = 0;
  CHECK_EQ((int)cyr::codepointAt(s, i), (int)'A');
  i += cyr::nextCodepoint(s, i);
  CHECK_EQ((int)cyr::codepointAt(s, i), 0x0411);
  i += cyr::nextCodepoint(s, i);
  CHECK_EQ((int)cyr::codepointAt(s, i), (int)'B');
}

static void test_text_width() {
  // Ширина считается в пикселях: ASCII — половина от cell,
  // кириллица — полная ширина cell. Формула: ceil для половины.
  cyr::setCellWidth(8);
  CHECK_EQ(cyr::textWidth("AB"), 8);        // 2 * 4
  CHECK_EQ(cyr::textWidth("АБ"), 16);       // 2 * 8
  CHECK_EQ(cyr::textWidth("AБ"), 12);       // 4 + 8
  CHECK_EQ(cyr::textWidth(""), 0);
}

int main() {
  test_count_codepoints_ascii();
  test_count_codepoints_cyrillic();
  test_cyrillic_bytes_are_two();
  test_decode_codepoint();
  test_next_steps_through_mixed();
  test_text_width();
  return puretest::summary("cyrillic");
}
```

- [ ] **Step 2: Прогнать, убедиться что не собирается**

Run:
```bash
tools/run_tests.sh
```
Expected: `СБОРКА ПРОВАЛЕНА: test_cyrillic`.

- [ ] **Step 3: Написать реализацию**

Создать `src/pure/cyrillic.h`:

```cpp
// Работа с UTF-8: посимвольный обход и оценка ширины строки.
// Ключевое свойство: обход всегда по границам кодпоинтов, поэтому
// разрезать кириллическую букву пополам структурно невозможно.
#pragma once

#include <cstdint>
#include <cstddef>

namespace cyr {

// Длина кодпоинта в байтах по его первому байту. 1..4.
inline size_t nextCodepoint(const char* s, size_t i) {
  const uint8_t c = (uint8_t)s[i];
  if (c == 0) return 0;
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;  // повреждённый байт — считаем одним, чтобы не зациклиться
}

inline size_t countCodepoints(const char* s) {
  size_t n = 0, i = 0;
  while (s[i] != 0) { i += nextCodepoint(s, i); ++n; }
  return n;
}

// Декодирование кодпоинта из позиции i. Возвращает 0xFFFD при ошибке.
inline uint32_t codepointAt(const char* s, size_t i) {
  const uint8_t c = (uint8_t)s[i];
  if (c == 0) return 0;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) {
    return (uint32_t)((c & 0x1F) << 6) | ((uint8_t)s[i + 1] & 0x3F);
  }
  if ((c & 0xF0) == 0xE0) {
    return (uint32_t)((c & 0x0F) << 12) |
           (uint32_t)(((uint8_t)s[i + 1] & 0x3F) << 6) |
           ((uint8_t)s[i + 2] & 0x3F);
  }
  if ((c & 0xF8) == 0xF0) {
    return (uint32_t)((c & 0x07) << 18) |
           (uint32_t)(((uint8_t)s[i + 1] & 0x3F) << 12) |
           (uint32_t)(((uint8_t)s[i + 2] & 0x3F) << 6) |
           ((uint8_t)s[i + 3] & 0x3F);
  }
  return 0xFFFD;
}

// Ширина одной ячейки моноширинного шрифта. ASCII занимает половину,
// всё остальное (кириллица и прочее) — целую.
inline int& cellWidthRef() { static int w = 8; return w; }
inline void setCellWidth(int px) { cellWidthRef() = px; }
inline int cellWidth() { return cellWidthRef(); }

inline int codepointWidth(uint32_t cp) {
  return (cp < 0x80) ? (cellWidth() + 1) / 2 : cellWidth();
}

inline int textWidth(const char* s) {
  int w = 0;
  size_t i = 0;
  while (s[i] != 0) {
    w += codepointWidth(codepointAt(s, i));
    i += nextCodepoint(s, i);
  }
  return w;
}

}  // namespace cyr
```

- [ ] **Step 4: Прогнать тесты**

Run:
```bash
tools/run_tests.sh
```
Expected: `PASS wav`, `PASS cyrillic`, `Все тесты прошли (2).`

- [ ] **Step 5: Commit**

```bash
git add src/pure/cyrillic.h test/test_cyrillic.cpp
git commit -m "feat: UTF-8 обход и ширина строки с тестами"
```

---

### Task 5: Чистый модуль base64

**Files:**
- Create: `src/pure/base64.h`
- Test: `test/test_base64.cpp`

- [ ] **Step 1: Написать провальный тест**

Создать `test/test_base64.cpp`:

```cpp
#include "pure_test.h"
#include "base64.h"

#include <vector>
#include <string>
#include <cstdint>

static void test_encode_known() {
  // Эталоны из RFC 4648, раздел 10.
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"", 0), "");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"f", 1), "Zg==");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"fo", 2), "Zm8=");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"foo", 3), "Zm9v");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"foob", 4), "Zm9vYg==");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"fooba", 5), "Zm9vYmE=");
  CHECK_STR_EQ(b64::encodeStr((const uint8_t*)"foobar", 6), "Zm9vYmFy");
}

static void test_decode_roundtrip() {
  std::vector<uint8_t> data(1000);
  for (size_t i = 0; i < data.size(); ++i) data[i] = (uint8_t)(i * 7 + 3);
  std::string enc = b64::encodeStr(data.data(), data.size());
  std::vector<uint8_t> back = b64::decode(enc);
  CHECK_EQ(back.size(), data.size());
  CHECK(back == data);
}

static void test_decoded_size() {
  CHECK_EQ(b64::decodedSize("Zg=="), (size_t)1);
  CHECK_EQ(b64::decodedSize("Zm8="), (size_t)2);
  CHECK_EQ(b64::decodedSize("Zm9v"), (size_t)3);
  CHECK_EQ(b64::decodedSize(""), (size_t)0);
}

int main() {
  test_encode_known();
  test_decode_roundtrip();
  test_decoded_size();
  return puretest::summary("base64");
}
```

- [ ] **Step 2: Прогнать, убедиться что не собирается**

Run:
```bash
tools/run_tests.sh
```
Expected: `СБОРКА ПРОВАЛЕНА: test_base64`.

- [ ] **Step 3: Написать реализацию**

Создать `src/pure/base64.h`:

```cpp
// Base64 (RFC 4648). Заголовок-only, чтобы компилироваться и на ESP32,
// и на Mac без общего .cpp.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace b64 {

inline const char* alphabet() {
  return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

inline int decodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

inline size_t encodedSize(size_t n) { return ((n + 2) / 3) * 4; }

inline size_t decodedSize(const char* s) {
  size_t len = 0;
  while (s[len] != 0) ++len;
  if (len == 0) return 0;
  size_t pad = 0;
  if (len >= 1 && s[len - 1] == '=') ++pad;
  if (len >= 2 && s[len - 2] == '=') ++pad;
  return (len / 4) * 3 - pad;
}

inline void encode(const uint8_t* data, size_t n, std::string& out) {
  out.clear();
  out.reserve(encodedSize(n));
  const char* a = alphabet();
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) |
                 (uint32_t)data[i + 2];
    out.push_back(a[(v >> 18) & 0x3F]);
    out.push_back(a[(v >> 12) & 0x3F]);
    out.push_back(a[(v >> 6) & 0x3F]);
    out.push_back(a[v & 0x3F]);
  }
  const size_t rem = n - i;
  if (rem == 1) {
    uint32_t v = (uint32_t)data[i] << 16;
    out.push_back(a[(v >> 18) & 0x3F]);
    out.push_back(a[(v >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out.push_back(a[(v >> 18) & 0x3F]);
    out.push_back(a[(v >> 12) & 0x3F]);
    out.push_back(a[(v >> 6) & 0x3F]);
    out.push_back('=');
  }
}

inline std::string encodeStr(const uint8_t* data, size_t n) {
  std::string s;
  encode(data, n, s);
  return s;
}

inline std::vector<uint8_t> decode(const std::string& s) {
  std::vector<uint8_t> out;
  out.reserve(decodedSize(s.c_str()));
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    if (c == '=') break;
    const int v = decodeChar(c);
    if (v < 0) continue;  // переносы строк и мусор пропускаем
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)((acc >> bits) & 0xFF));
    }
  }
  return out;
}

}  // namespace b64
```

- [ ] **Step 4: Прогнать тесты**

Run:
```bash
tools/run_tests.sh
```
Expected: `PASS wav`, `PASS cyrillic`, `PASS base64`,
`Все тесты прошли (3).`

- [ ] **Step 5: Commit**

```bash
git add src/pure/base64.h test/test_base64.cpp
git commit -m "feat: base64 с тестами"
```

---

### Task 6: Прошивка милестоуна 0

**Files:**
- Create: `include/probe.h`
- Create: `src/probe_main.cpp`

- [ ] **Step 1: Написать `include/probe.h`**

```cpp
// Общие определения прошивки милестоуна 0.
#pragma once

#include <cstdint>
#include <cstddef>

namespace probe {

constexpr uint32_t kSampleRate   = 16000;
constexpr uint32_t kRecordMs     = 3000;   // 3 секунды для быстрой проверки
constexpr size_t   kAudioBytes   = (size_t)kSampleRate * 2 * (kRecordMs / 1000);
// == 96000 байт аудио, 96044 байта с заголовком
constexpr size_t   kDmaChunk     = 8192;   // кусок захвата в DRAM
constexpr size_t   kBase64Chunk  = 768;    // кратно 3: без набивки в потоке

}  // namespace probe
```

Пояснение к `kBase64Chunk`: 768 кратно 3, поэтому каждый кусок кодируется в
ровно 1024 символа без `=`-набивки. Это позволяет стримить base64 по частям,
не склеивая всё в памяти, и без риска, что `=` попадёт в середину.

- [ ] **Step 2: Написать прошивку**

Создать `src/probe_main.cpp`:

```cpp
// Милестоун 0: проверить PSRAM и PDM-микрофон.
//
// Пишет kRecordMs секунд, затем стримит запись по Serial в base64.
// Скрипт tools/capture.py собирает из этого WAV.
//
// Протокол вывода:
//   задаёт темп:  "PROBE META <dataBytes> <sampleRate>\n"
//   строки base64: строки ровно по 64 символа, разделитель '\n'
//   конец:         "PROBE END\n"

#include <Arduino.h>
#include <M5Unified.h>

#include "probe.h"
#include "pure/base64.h"
#include "pure/wav.h"

namespace {

uint8_t* g_audio = nullptr;   // буфер в PSRAM: 44 байта заголовка + аудио
uint8_t  g_chunk[probe::kDmaChunk];

// Собирает одну строку base64 (не более 64 символов) и печатает её.
void emitBase64(const uint8_t* data, size_t n) {
  static std::string line;
  line.clear();
  b64::encode(data, n, line);
  for (size_t i = 0; i < line.size(); ++i) {
    Serial.write((uint8_t)line[i]);
    if ((i + 1) % 64 == 0) Serial.write('\n');
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  auto cfg = M5.config();
  M5.begin(cfg);

  // --- Шаг 1: PSRAM ---
  const uint32_t freePsram = ESP.getFreePsram();
  Serial.printf("[probe] PSRAM найдена: %u байт свободно\n", freePsram);
  Serial.printf("[probe] Кучи всего: %u байт\n", ESP.getFreeHeap());
  if (freePsram == 0) {
    Serial.println("[probe] ОШИБКА: PSRAM нет. Проверь -DBOARD_HAS_PSRAM.");
    while (true) delay(1000);
  }

  // --- Шаг 2: буфер ---
  const size_t total = wav::totalSize(probe::kAudioBytes);
  g_audio = (uint8_t*)ps_malloc(total);
  if (g_audio == nullptr) {
    Serial.printf("[probe] ОШИБКА: не удалось выделить %u байт в PSRAM\n",
                  (unsigned)total);
    while (true) delay(1000);
  }
  Serial.printf("[probe] Буфер: %u байт в PSRAM\n", (unsigned)total);

  // --- Шаг 3: микрофон ---
  if (!M5.Mic.begin()) {
    Serial.println("[probe] ОШИБКА: M5.Mic.begin() не удался");
    while (true) delay(1000);
  }
  Serial.printf("[probe] Микрофон запущен, %u Гц\n", probe::kSampleRate);

  // --- Шаг 4: захват ---
  Serial.printf("[probe] Запись: %.1f с\n", probe::kRecordMs / 1000.0f);
  const uint32_t started = millis();
  size_t filled = 0;
  while (filled < probe::kAudioBytes &&
         (millis() - started) < (probe::kRecordMs + 2000)) {
    const size_t want = min(probe::kDmaChunk, probe::kAudioBytes - filled);
    if (!M5.Mic.record(g_chunk, want, probe::kSampleRate)) {
      delay(1);
      continue;
    }
    // Ждём, пока запись в DRAM завершится.
    while (M5.Mic.isRecording()) delay(1);
    // DMA не пишет в PSRAM — копируем вручную.
    memcpy(g_audio + wav::headerSize() + filled, g_chunk, want);
    filled += want;
  }
  M5.Mic.end();

  const uint32_t elapsed = millis() - started;
  Serial.printf("[probe] Записано %u байт за %u мс\n",
                (unsigned)filled, elapsed);
  if (filled == 0) {
    Serial.println("[probe] ОШИБКА: ни одного байта не записано");
    while (true) delay(1000);
  }

  // --- Шаг 5: заголовок и вывод ---
  wav::writeHeader(g_audio, (uint32_t)filled, probe::kSampleRate);
  Serial.printf("PROBE META %u %u\n", (unsigned)filled, probe::kSampleRate);

  const uint32_t t0 = millis();
  for (size_t off = 0; off < filled; off += probe::kBase64Chunk) {
    const size_t n = min(probe::kBase64Chunk, filled - off);
    // Кусок может не быть кратным 3 только последним. Тогда добавляем
    // набивку, а затем закрываем строку.
    emitBase64(g_audio + wav::headerSize() + off, n);
    if (n % 3 != 0) Serial.write('\n');
  }
  Serial.println("PROBE END");
  Serial.printf("[probe] Отправлено %u байт, кодирование %u мс\n",
                (unsigned)filled, millis() - t0);
  Serial.println("[probe] Готово.");
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 3: Проверить, что чистые модули видны заголовкам**

Заголовок `#include "pure/base64.h"` компилятор ESP32 найдёт, потому что
`src/` лежит в путях поиска PlatformIO. Проверить локально можно так — модули
не зависят от Arduino:

Run:
```bash
clang++ -std=c++17 -fsyntax-only -Isrc -x c++ src/pure/base64.h
clang++ -std=c++17 -fsyntax-only -Isrc -x c++ src/pure/wav.h
```
Expected: обе команды без вывода (успех).

- [ ] **Step 4: Commit**

```bash
git add include/probe.h src/probe_main.cpp
git commit -m "feat: прошивка милестоуна 0 для проверки PSRAM и микрофона"
```

---

### Task 7: Скрипт сбора WAV

**Files:**
- Create: `tools/capture.py`

- [ ] **Step 1: Написать скрипт**

Создать `tools/capture.py`:

```python
#!/usr/bin/env python3
"""Читает base64 со Serial и складывает в WAV.

Использование:
    pio device monitor -b 115200 | python3 tools/capture.py test.wav

Протокол (см. src/probe_main.cpp):
    PROBE META <dataBytes> <sampleRate>
    <строки base64>
    PROBE END
"""
import base64
import struct
import sys


def write_wav(path, pcm, sample_rate):
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate,
                                    sample_rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    with open(path, "wb") as f:
        f.write(header)
        f.write(pcm)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "test.wav"
    data_bytes = None
    sample_rate = 16000
    chunks = []
    seen_end = False

    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("PROBE META"):
            parts = line.split()
            if len(parts) >= 4:
                data_bytes = int(parts[2])
                sample_rate = int(parts[3])
                print(f"[capture] ожидаю {data_bytes} байт, "
                      f"{sample_rate} Гц", file=sys.stderr, flush=True)
            continue
        if line == "PROBE END":
            seen_end = True
            break
        if line.startswith("[probe]") or line.startswith("PROBE"):
            print(line, file=sys.stderr, flush=True)
            continue
        chunks.append(line)

    if data_bytes is None:
        print("[capture] ОШИБКА: не дождался строки PROBE META. "
              "Устройство не перезагрузилось или прошивка не та.",
              file=sys.stderr)
        return 1
    if not seen_end:
        print("[capture] предупреждение: PROBE END не пришёл, "
              "собираю что есть", file=sys.stderr)

    b64_text = "".join(chunks)
    try:
        pcm = base64.b64decode(b64_text, validate=True)
    except Exception as exc:
        print(f"[capture] ОШИБКА: base64 не разобрался: {exc}",
              file=sys.stderr)
        return 1

    if len(pcm) != data_bytes:
        print(f"[capture] ОШИБКА: получил {len(pcm)} байт, "
              f"ждал {data_bytes}. Похоже на обрыв.", file=sys.stderr)
        return 1

    write_wav(out_path, pcm, sample_rate)
    duration = len(pcm) / (sample_rate * 2)
    print(f"[capture] Готово: {out_path} ({len(pcm)} байт, "
          f"{duration:.1f} с)", file=sys.stderr)
    print(f"[capture] Послушать: afplay {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Проверить разбор на синтетическом вводе**

Скрипт тестируется без железа. Сгенерировать фальшивый поток и подать на
вход:

Run:
```bash
python3 - <<'PY' | python3 tools/capture.py /tmp/fake.wav
import base64
pcm = bytes(range(256)) * 4          # 1024 байта
print("PROBE META %d 16000" % len(pcm))
b = base64.b64encode(pcm).decode()
for i in range(0, len(b), 64):
    print(b[i:i+64])
print("PROBE END")
PY
```
Expected: `[capture] Готово: /tmp/fake.wav (1024 байт, 0.0 с)` и код 0.

Проверить, что WAV валиден:

Run:
```bash
python3 - <<'PY'
import wave
w = wave.open("/tmp/fake.wav")
print(w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes())
w.close()
PY
```
Expected: `1 2 16000 512` — моно, 16 бит, 16 кГц, 512 сэмплов.

- [ ] **Step 3: Проверить, что битый ввод даёт честную ошибку**

Run:
```bash
printf 'PROBE META 100 16000\nAAAA\n' | python3 tools/capture.py /tmp/bad.wav; echo "код: $?"
```
Expected: `[capture] ОШИБКА: получил 3 байт, ждал 100` и `код: 1`.

- [ ] **Step 4: Commit**

```bash
git add tools/capture.py
git commit -m "feat: сборка WAV из base64 по Serial"
```

---

### Task 8: Проверка на живом устройстве

Здесь нужен пользователь: PlatformIO Core не установлен, и сборка требует
сети. Этот шаг выполняется не тобой, а пользователем; твоя задача — выдать
точные команды и потом разобрать результат.

- [ ] **Step 1: Попросить пользователя установить PlatformIO**

```bash
uv tool install platformio
```

- [ ] **Step 2: Собрать и прошить**

```bash
pio run -e esp32dev -t upload
```
Expected: `SUCCESS` в конце. Если виден `Connecting........`, значит плата не
в режиме загрузки — пользователь зажимает кнопку A (или ту, что у StickC
Plus2 отвечает за загрузку) и повторяет.

- [ ] **Step 3: Записать и собрать WAV**

```bash
pio device monitor -b 115200 | python3 tools/capture.py test.wav
```
Expected в потоке:
```
[probe] PSRAM найдена: 2097152 байт свободно
[probe] Буфер: 96044 байт в PSRAM
[probe] Микрофон запущен, 16000 Гц
[probe] Запись: 3.0 с
[probe] Записано 96000 байт за ~3100 мс
[capture] Готово: test.wav (96000 байт, 3.0 с)
```

- [ ] **Step 4: Прослушать**

```bash
afplay test.wav
```
Expected: слышна речь, записанная в течение трёх секунд, без хрипа и без
сплошного шипения.

- [ ] **Step 5: Разобрать результат**

| Что слышно | Диагноз | Действие |
|---|---|---|
| Чистая речь | Всё верно | Переходим к следующему милестоуну |
| Очень тихо | `magnification` мал | Увеличить в `M5.Mic.config()` |
| Оглушительно, клиппинг | `magnification` велик | Уменьшить |
| Белый шум | Данные не доходят | Проверить копирование DRAM→PSRAM и `pin_data_in/pin_ws` |
| Тишина | Микрофон не стартовал | Проверить `M5.Mic.begin()` и питание микрофона |
| Прерывисто, рывками | Захват не успевает | Увеличить `kDmaChunk` или уменьшить `over_sampling` |
| Файл не собрался | Обрыв Serial | Проверить `kBase64Chunk` и скорость порта |

Записать вывод в отчёт.

---

## Самопроверка плана

**Покрытие спеки.** Раздел 2 (железо): Task 2 (`BOARD_HAS_PSRAM`, `esp32dev`) и
Task 6 (проверка PSRAM и PDM). Раздел 11 `Милестоун 0`: Tasks 1–8 целиком.
Раздел 6 (аудио, копирование DRAM→PSRAM): Task 6, Step 4. Раздел 8 (шрифт):
намеренно вне этого плана — нужно для UI, не для проверки железа. Раздел 9
(ошибки) и 10 (журнал): вне милестоуна 0, начинают работать в милестоуне 2+.
Разделы 5, 7: будущие милестоуны.

**Плейсхолдеры.** Не осталось: каждый шаг с кодом содержит полный код, каждая
команда — точную команду с ожидаемым выводом. Единственное исключение —
Task 8, который по своей природе выполняется пользователем и потому описан
таблицей диагнозов, а не кодом.

**Согласованность типов.** `wav::headerSize()`, `wav::totalSize()`,
`wav::writeHeader(buf, dataBytes, sampleRate)` — совпадают в Tasks 3, 6, 7.
`b64::encode`, `b64::encodeStr`, `b64::decode`, `b64::decodedSize` — Task 5 и
Task 6 используют `encode`. `cyr::setCellWidth`, `cyr::textWidth`,
`cyr::codepointAt`, `cyr::nextCodepoint` — Task 4. `probe::kAudioBytes`,
`probe::kDmaChunk`, `probe::kBase64Chunk`, `probe::kSampleRate`,
`probe::kRecordMs` — Task 6, все определены в `include/probe.h`.

**Известный риск.** `M5.Mic.record()` может вернуть `true`, но фактически
записать меньше, если очередь занята. В Task 6 цикл ждёт `isRecording() == 0`
перед копированием — при расхождении `filled` в Serial будет видно
(`Записано N байт` меньше 96000). Это ожидаемая точка диагностики, а не
скрытый баг.
