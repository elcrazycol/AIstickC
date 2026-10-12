# Милестоун 2: ответ LLM через OpenCode Go — план реализации

**Goal:** распознанный текст уходит в DeepSeek (`deepseek-v4.1-flash`) через
OpenCode Go, ответ виден на экране.

**Architecture:** Прокси не нужен — OpenCode доступен из России напрямую.
Тело запроса — небольшой JSON, собирается целиком в памяти (в отличие от
multipart STT, тут второй буфер не нужен). Сборка запроса и разбор ответа
вынесены в чистые модули и тестируются на Mac; сетевой слой повторяет
терпеливую отправку/чтение, уже проверенную в STT.

**Tech Stack:** PlatformIO, Arduino ESP32, M5Unified, `WiFiClientSecure`.

---

## Контекст для исполнителя

Проект: `/Users/lesha/codes/aistick`, милестоуны 0–1 сделаны и проверены на
железе (см. `docs/superpowers/specs/2026-10-11-voice-assistant-design.md` и
`docs/superpowers/plans/2026-10-11-milestone-1-stt.md`).

Что известно и обязательно к учёту:

- **`pio` из Bash-инструмента не работает** — сборка и прошивка только из
  терминала пользователя.
- **Тесты — только `tools/run_tests.sh`** (clang++, без PlatformIO и сети).
- **OpenCode Go требует заголовок `x-opencode-session`.** Без него эндпоинт
  отвечает `400` с `Request is missing x-opencode-session`. Значение —
  произвольная стабильная строка; формат `ses_<hex>` достаточен.
- **Ключ подписки — `OPENCODE_API_KEY`** в `include/config.h` (в git не
  попадает).
- **Эндпоинт:** `POST https://opencode.ai/zen/go/v1/chat/completions`,
  `Authorization: Bearer <ключ>`, OpenAI-совместимый JSON.
- **Модель:** `deepseek-v4.1-flash`.
- **Ответ:** `choices[0].message.content`. У reasoning-моделей рядом может
  лежать `reasoning_content`; `content` при исчерпании `max_tokens` бывает
  пустым.
- Версию «identify itself with its own user agent» из документации Go
  закрываем заголовком `User-Agent: aistick/0.1`.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `src/pure/llm_request.h` | ЧИСТЫЙ: сборка JSON-запроса, экранирование строк |
| `src/pure/json_extract.h` | ЧИСТЫЙ: дополняется разбором по пути `choices/[0]/message/content` |
| `src/net.cpp/.h` | Дополняется `writeAll`/`readAll` (общие с STT) |
| `src/stt.cpp` | Переводится на `net::writeAll`/`net::readAll` (без смены поведения) |
| `src/llm.cpp/.h` | Запрос к OpenCode Go, разбор ответа |
| `include/settings.h` | Дополняется: хост, путь, модель, системный промпт, лимиты |
| `src/main.cpp` | Состояние Thinking, вызов LLM после STT, вывод ответа |
| `test/test_llm_request.cpp` | Тесты сборки запроса (в т.ч. round-trip через json_extract) |
| `test/test_json.cpp` | Тесты разбора вложенного пути |

---

## Task 1: Разбор JSON по пути

`json_extract.h` умеет только плоские ключи верхнего уровня. Ответ LLM
вложенный: `choices` → массив → `[0]` → `message` → `content`.

Добавляются:

- `selectKey(s, i, key)` — позиционирует `i` на значении ключа в объекте;
- `selectIndex(s, i, n)` — позиционирует `i` на n-м элементе массива;
- `extractPath(json, "choices/[0]/message/content", out)` — строка по пути.

Тесты в `test/test_json.cpp`: вложенное значение, `reasoning_content` рядом,
ошибка `error/message`, пустой массив `choices`, обрыв, индекс `[1]`,
escape-последовательности внутри `content`.

## Task 2: Чистый модуль сборки запроса

`src/pure/llm_request.h`:

- `escapedLen(s)` — сколько байт займёт экранированная строка;
- `writeEscaped(out, s)` — экранирование JSON (`"` `\` и управляющие символы
  в `\u00XX`, байты UTF-8 проходят как есть);
- `build(model, system, user, maxTokens)` → тело запроса.

Тесты в `test/test_llm_request.cpp`: кавычки/слэши/переводы строк, кириллица
не экранируется, управляющий символ → `\u00XX`, длина совпадает с
`escapedLen`, и главное — **round-trip**: собранное тело разбирается обратно
`jsonx::extractPath(body, "messages/[1]/content", out)` и даёт исходный текст.

## Task 3: Общий сетевой ввод-вывод

`net::writeAll(c, data, len, started)` и `net::readAll(c, started, maxLen)`
поднимаются из `stt.cpp` в `net.cpp`. Поведение то же (терпеливая запись,
чтение до закрытия или простоя 1500 мс, общий таймаут `kHttpReadMs`), но
теперь одна реализация на STT и LLM.

## Task 4: Модуль LLM

`src/llm.cpp/.h`: `llm::ask(const String& userText) -> llm::Reply`.
Заголовки `Authorization`, `x-opencode-session`, `User-Agent`,
`Content-Type`, `Content-Length`; тело из `llmreq::build`; разбор
`choices[0].message.content`; при пустом `content` — внятная ошибка с
`finish_reason`.

Сессия — `ses_` + 32 hex из `esp_random()`, генерируется один раз за время
работы устройства.

## Task 5: Прошивка

`main.cpp`: после успешного STT — состояние `Thinking`, вызов `llm::ask`,
вывод ответа. BtnB чистит, BtnA в состоянии показа начинает новую запись.

## Task 6: Проверка на устройстве

1. `pio run -e esp32dev -t upload`
2. Зажать BtnA, спросить по-русски, отпустить.
3. Ожидается Serial:
   ```
   [main] текст: <распознанное>
   [llm] ответ 200 за ~2500 мс
   [main] ответ: <ответ модели>
   ```
4. Разбор отказов: `HTTP 400 MissingSessionID` — заголовок не дошёл;
   `HTTP 401` — ключ; `HTTP 529/503` — провайдер занят, повторить;
   пустой ответ — модель израсходовала `max_tokens` на reasoning, поднять
   `kLlmMaxTokens`.

## Самопроверка плана

**Покрытие спеки.** Раздел 7 (LLM → OpenCode Go, JSON, ответ
`choices[0].message.content`): Tasks 1, 2, 4. Требование
`x-opencode-session` и `User-Agent`: Task 4. Состояние `LLM`/`Thinking`
(раздел 5): Task 5. Таблица ошибок (раздел 9): Task 4, 6.

**Плейсхолдеры.** Не осталось.

**Согласованность.** `llmreq::build/escapedLen` — Task 2 и 4.
`jsonx::extractPath` — Task 1 и 4. `llm::ask/llm::Reply` — Task 4 и 5.
`net::writeAll/readAll` — Task 3, 4 и (после рефакторинга) `stt.cpp`.

**Известный риск.** `deepseek-v4.1-flash` — reasoning-модель: при
`max_tokens=150` часть бюджета уходит в `reasoning_content`. Если ответы
будут приходить пустыми, поднять лимит или выбрать не-reasoning модель.
