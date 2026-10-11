# aistick

Карманный голосовой ассистент на M5StickC PLUS2.
Зажал кнопку → записал → распознал (Groq Whisper) → ответ (DeepSeek через
OpenCode Go) → прочитал на экране.

## Установка инструментов

PlatformIO Core на этой машине не установлен:

```bash
uv tool install platformio
```

## Секреты

```bash
cp include/config.h.example include/config.h
```

Заполни `include/config.h`: ключ Groq, ключ OpenCode Go, SSID и пароль Wi-Fi.
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

## Тесты чистой логики

Компилируются clang++ и не требуют ни PlatformIO, ни железа:

```bash
tools/run_tests.sh
```

## Почему так

Подробности в `docs/superpowers/specs/2026-10-11-voice-assistant-design.md`.
Коротко: чип здесь ESP32-PICO-V3-02, а не S3 из исходного ТЗ; микрофон PDM,
а не I2S; PSRAM надо включать флагом `BOARD_HAS_PSRAM`.
