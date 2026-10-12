#!/usr/bin/env python3
"""Прокси распознавания речи: стик -> этот сервер -> Groq Whisper.

Зачем: api.groq.com недоступен из России напрямую (403, гео-блок).
Сервер стоит в Европе, ходит в Groq сам, а стик разговаривает с сервером.
Бонус: ключ Groq живёт здесь, а не в прошивке.

Стик отправляет СЫРОЙ PCM (16 бит, little-endian, моно, 16 кГц) —
WAV-заголовок и multipart собираются здесь, где память не в дефиците.

Только стандартная библиотека Python: на сервере уже работает VPN, и
ставить туда новый рантайм не нужно (диск занят на 92%).

Переменные окружения:
    GROQ_API_KEY  ключ Groq (обязательно)
    AUTH_TOKEN    общий секрет, стик присылает в X-Auth (обязательно)
    PORT          порт прослушивания (по умолчанию 9443)
    TLS_CERT      путь к сертификату (обязательно)
    TLS_KEY       путь к ключу (обязательно)
    STT_MODEL     модель Groq (по умолчанию whisper-large-v3-turbo)
    STT_LANG      язык (по умолчанию ru)
    MAX_BYTES     максимум тела запроса (по умолчанию 1 МБ)
"""

import hmac
import json
import os
import ssl
import sys
import time
import urllib.error
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

GROQ_URL = "https://api.groq.com/openai/v1/audio/transcriptions"
GROQ_TIMEOUT = 30
SAMPLE_RATE = 16000

PORT = int(os.environ.get("PORT", "9443"))
GROQ_KEY = os.environ.get("GROQ_API_KEY", "")
AUTH_TOKEN = os.environ.get("AUTH_TOKEN", "")
TLS_CERT = os.environ.get("TLS_CERT", "")
TLS_KEY = os.environ.get("TLS_KEY", "")
MODEL = os.environ.get("STT_MODEL", "whisper-large-v3-turbo")
LANG = os.environ.get("STT_LANG", "ru")
MAX_BYTES = int(os.environ.get("MAX_BYTES", str(1024 * 1024)))

# Меньше 50 мс распознавать нечего.
MIN_BYTES = 1600


def wav_header(data_len, sample_rate=SAMPLE_RATE):
    """WAV-заголовок (44 байта) для сырого PCM: моно, 16 бит."""
    import struct
    return (
        b"RIFF"
        + struct.pack("<I", 36 + data_len)
        + b"WAVE"
        + b"fmt "
        + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate,
                      sample_rate * 2, 2, 16)
        + b"data"
        + struct.pack("<I", data_len)
    )


def build_multipart(boundary, fields, filename, data, content_type):
    """Тело multipart/form-data для Groq."""
    b = boundary.encode()
    out = []
    for name, value in fields.items():
        out.append(
            b"--" + b + b"\r\n"
            + b'Content-Disposition: form-data; name="' + name.encode()
            + b'"\r\n\r\n' + value.encode() + b"\r\n"
        )
    out.append(
        b"--" + b + b"\r\n"
        + b'Content-Disposition: form-data; name="file"; filename="'
        + filename.encode() + b'"\r\n'
        + b"Content-Type: " + content_type.encode() + b"\r\n\r\n"
    )
    out.append(data)
    out.append(b"\r\n--" + b + b"--\r\n")
    return b"".join(out)


def call_groq(body, boundary):
    """Отправляет тело в Groq. Возвращает (код, текст)."""
    req = urllib.request.Request(
        GROQ_URL,
        data=body,
        method="POST",
        headers={
            "Authorization": "Bearer " + GROQ_KEY,
            "Content-Type": "multipart/form-data; boundary=" + boundary,
            # Groq просит осмысленный User-Agent.
            "User-Agent": "aistick-proxy/1.0",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=GROQ_TIMEOUT) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001 — сеть бывает разной
        return 0, str(e)


class Handler(BaseHTTPRequestHandler):
    server_version = "aistick-stt-proxy/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        # Свой формат вместо стандартного, без даты в квадратных скобках.
        sys.stdout.write("[proxy] " + (fmt % args) + "\n")
        sys.stdout.flush()

    def _json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):  # noqa: N802 — имя задано базовым классом
        t0 = time.time()

        if not self.path.startswith("/stt"):
            return self._json(404, {"error": "not found"})

        # Общий секрет: без него любой жёг бы квоту Groq.
        token = self.headers.get("X-Auth", "")
        if not hmac.compare_digest(token, AUTH_TOKEN):
            self.log_message("отказ: неверный X-Auth")
            return self._json(401, {"error": "unauthorized"})

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return self._json(400, {"error": "bad content-length"})

        if length <= 0:
            return self._json(400, {"error": "empty body"})
        if length > MAX_BYTES:
            return self._json(413, {"error": "audio too large"})

        pcm = self.rfile.read(length)
        if len(pcm) != length:
            return self._json(400, {"error": "short read"})
        if len(pcm) < MIN_BYTES:
            return self._json(400, {"error": "audio too short"})

        wav = wav_header(len(pcm)) + pcm
        boundary = "----aistickproxy" + uuid.uuid4().hex
        body = build_multipart(
            boundary,
            {"model": MODEL, "response_format": "json", "language": LANG},
            "audio.wav", wav, "audio/wav",
        )

        code, text = call_groq(body, boundary)
        ms = int((time.time() - t0) * 1000)

        if code == 0:
            self.log_message("сеть: %s (%d мс)", text[:120], ms)
            return self._json(502, {"error": "groq unreachable: " + text[:120]})

        if code != 200:
            self.log_message("Groq %d: %s (%d мс)", code, text[:200], ms)
            return self._json(code, {"error": "groq %d" % code,
                                     "detail": text[:200]})

        try:
            parsed = json.loads(text)
        except ValueError:
            self.log_message("Groq вернул не JSON: %s", text[:200])
            return self._json(502, {"error": "bad json from groq"})

        result = parsed.get("text", "")
        if not isinstance(result, str):
            result = ""
        self.log_message("ок: %d байт PCM -> %d симв. за %d мс",
                         len(pcm), len(result), ms)
        return self._json(200, {"text": result})


def main():
    missing = [n for n, v in (("GROQ_API_KEY", GROQ_KEY),
                              ("AUTH_TOKEN", AUTH_TOKEN),
                              ("TLS_CERT", TLS_CERT),
                              ("TLS_KEY", TLS_KEY)) if not v]
    if missing:
        sys.stderr.write("[proxy] ОШИБКА: не заданы " + ", ".join(missing) + "\n")
        return 1

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(TLS_CERT, TLS_KEY)

    httpd = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

    sys.stdout.write(
        "[proxy] слушаю :%d, модель %s, язык %s, лимит %d байт\n"
        % (PORT, MODEL, LANG, MAX_BYTES))
    sys.stdout.flush()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
