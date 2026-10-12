# aistick

Карманный голосовой ассистент на M5StickC PLUS2.
Зажал кнопку → записал → распознал (Groq Whisper) → ответ (DeepSeek через
OpenCode Go) → прочитал на экране.

## Как устроено распознавание

`api.groq.com` **недоступен из России напрямую** (403, гео-блок). Поэтому
стик ходит к своему прокси на VPS, а тот — в Groq:

```
стик ──HTTPS──> VPS (deploy/stt-proxy.py) ──HTTPS──> Groq Whisper
                    └── ключ Groq живёт здесь, в прошивке его нет
```

Стик отправляет сырой PCM (16 бит, LE, моно, 16 кГц); WAV-заголовок и
multipart собирает сервер. Прошивка от этого только проще.

## Как устроен ответ

`opencode.ai` **доступен из России напрямую**, поэтому прокси тут не нужен:

```
стик ──HTTPS──> opencode.ai/zen/go/v1/chat/completions ──> DeepSeek
          (Authorization: Bearer <ключ>, x-opencode-session)
```

Модель — `deepseek-v4.1-flash`. Тело запроса — небольшой JSON
(`model`, `messages`, `max_tokens`), ответ читается из
`choices[0].message.content`. Go просит клиентов присылать свой
`User-Agent` и стабильный `x-opencode-session` — оба заголовка уходят.

## Экран

Управление:

| Действие | Что делает |
|---|---|
| BtnA (зажать) | запись, отпустить — отправить |
| BtnA с экрана ответа | новый вопрос |
| BtnB (тап) | следующая страница; на последней — очистка |
| BtnB (удержать) | очистка сразу |

Шрифт — сгенерированный **JetBrains Mono** с кириллицей
(`include/font_jbmono.h`, OFL 1.1). Встроенные шрифты M5GFX кириллицу не
умеют, поэтому глифы собираются скриптом:

```bash
python3 tools/make_font.py --ttf JetBrainsMono-Regular.ttf \
    --out include/font_jbmono.h --size 14
```

Результат коммитится — для сборки Python не нужен. Текст переносится по
словам и разбивается на страницы чистым модулем `src/pure/text_wrap.h`
(тесты — `test/test_text_wrap.cpp`).

## Установка инструментов

PlatformIO Core на этой машине не установлен:

```bash
uv tool install platformio
```

## Секреты

```bash
cp include/config.h.example include/config.h
```

Развёртывание прокси описано в [deploy/README.md](deploy/README.md), там же
одна команда для заполнения `config.h`:

```bash
bash deploy/fill-config.sh
```

Она забирает токен с сервера и прописывает `PROXY_HOST`, `PROXY_PORT`,
`PROXY_PATH`, `PROXY_TOKEN`. Ключ Groq в прошивку не попадает вообще.

Остальное в `config.h`:

- `OPENCODE_API_KEY` — ключ подписки OpenCode Go
- `WIFI_SSID`, `WIFI_PASS` — сеть **2.4 ГГц**, ESP32 не видит 5 ГГц

Файл в `.gitignore` и в репозиторий не попадает.

## Милестоун 0: проверить микрофон и PSRAM

```bash
pio run -e esp32dev -t upload
python3 tools/capture.py test.wav
```

Скрипт сам открывает serial-порт и перезагружает плату. `pio device monitor`
для этого не подходит: он отказывается работать с перенаправленным stdin.
Если порт другой — `--port /dev/cu.usbserial-XXXX`.

Потом послушать:

```bash
afplay test.wav
```

Ожидаемо в выводе:

    [probe] PSRAM найдена: 2097152 байт свободно
    [probe] Буфер: 96044 байт в PSRAM
    [probe] Запись: 3.0 с
    [probe] Записано 96000 байт за ~3100 мс
    [capture] Готово: test.wav (96000 байт, 3.0 с)

## Смотреть лог устройства

```bash
python3 tools/capture.py --monitor
```

Скрипт сам открывает порт и перезагружает плату. В конце возвращает её в
рабочий режим, а не в загрузчик.

## Тесты чистой логики

Компилируются clang++ и не требуют ни PlatformIO, ни железа:

```bash
tools/run_tests.sh
```

## Почему так

Подробности в `docs/superpowers/specs/2026-10-11-voice-assistant-design.md`.
Коротко: чип здесь ESP32-PICO-V3-02, а не S3 из исходного ТЗ; микрофон PDM,
а не I2S; PSRAM надо включать флагом `BOARD_HAS_PSRAM`.
