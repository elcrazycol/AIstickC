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

Управление Tuya-выключателем (эндпоинт POST /tuya):
    TUYA_CLIENT_ID  Access ID из Tuya IoT Platform
    TUYA_SECRET     Access Secret
    TUYA_DEVICE_ID  Device ID выключателя
    TUYA_REGION     eu | us | cn | in (по умолчанию eu)
    TUYA_DP_PREFIX  префикс кода канала (по умолчанию switch_)

Секреты Tuya живут здесь, а не в прошивке: стик шлёт только
{"channel": 1, "state": "on|off|toggle"} и общий X-Auth.
"""

import hashlib
import hmac
import json
import os
import ssl
import sys
import time
import urllib.error
import urllib.parse
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

# --- Tuya Cloud (управление выключателем) ---
TUYA_CLIENT_ID = os.environ.get("TUYA_CLIENT_ID", "")
TUYA_SECRET = os.environ.get("TUYA_SECRET", "")
TUYA_DEVICE_ID = os.environ.get("TUYA_DEVICE_ID", "")
TUYA_REGION = os.environ.get("TUYA_REGION", "eu")
TUYA_DP_PREFIX = os.environ.get("TUYA_DP_PREFIX", "switch_")
# Каналы для команды «весь свет сразу» (channel = 0 или "all").
TUYA_CHANNELS = [int(x) for x in
                 os.environ.get("TUYA_CHANNELS", "1,2,3").split(",")
                 if x.strip().isdigit()]
TUYA_TIMEOUT = 15

TUYA_HOSTS = {
    "eu": "openapi.tuyaeu.com",
    "us": "openapi.tuyaus.com",
    "cn": "openapi.tuyacn.com",
    "in": "openapi.tuyain.com",
}
TUYA_HOST = TUYA_HOSTS.get(TUYA_REGION, TUYA_HOSTS["eu"])

# Кэш токена: живёт сутки, берём один раз.
_tuya_token = None
_tuya_token_until = 0.0


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


def _tuya_sign(method, path, body_str, token, t):
    """Подпись запроса к Tuya Cloud (HMAC-SHA256 по их схеме)."""
    body_hash = hashlib.sha256(body_str.encode()).hexdigest()
    string_to_sign = "%s\n%s\n\n%s" % (method, body_hash, path)
    msg = TUYA_CLIENT_ID + (token or "") + t + string_to_sign
    return hmac.new(TUYA_SECRET.encode(), msg.encode(),
                    hashlib.sha256).hexdigest().upper()


def _tuya_call(method, path, body=None, token=None):
    """Запрос к Tuya Cloud. Возвращает разобранный JSON."""
    t = str(int(time.time() * 1000))
    body_str = json.dumps(body, separators=(",", ":")) if body is not None else ""
    headers = {
        "client_id": TUYA_CLIENT_ID,
        "sign": _tuya_sign(method, path, body_str, token, t),
        "t": t,
        "sign_method": "HMAC-SHA256",
    }
    if token:
        headers["access_token"] = token
    data = None
    if body is not None:
        headers["Content-Type"] = "application/json"
        data = body_str.encode()
    req = urllib.request.Request("https://" + TUYA_HOST + path, data=data,
                                 method=method, headers=headers)
    with urllib.request.urlopen(req, timeout=TUYA_TIMEOUT) as resp:
        return json.loads(resp.read().decode("utf-8", "replace"))


def tuya_token():
    """Токен доступа с кэшем."""
    global _tuya_token, _tuya_token_until
    if _tuya_token and time.time() < _tuya_token_until:
        return _tuya_token
    r = _tuya_call("GET", "/v1.0/token?grant_type=1")
    if not r.get("success"):
        raise RuntimeError("token: %s" % r.get("msg", r))
    res = r["result"]
    _tuya_token = res["access_token"]
    _tuya_token_until = time.time() + int(res.get("expire_time", 3600)) - 60
    return _tuya_token


def tuya_channel_state(channel):
    """Текущее состояние канала: True/False или None, если не узнать."""
    code = TUYA_DP_PREFIX + str(channel)
    r = _tuya_call("GET", "/v1.0/devices/%s/status" % TUYA_DEVICE_ID,
                   token=tuya_token())
    if not r.get("success"):
        raise RuntimeError("status: %s" % r.get("msg", r))
    for item in r.get("result", []):
        if item.get("code") == code:
            return bool(item.get("value"))
    return None


def tuya_command(channel, state):
    """Отправляет команду каналу. state: True/False. Возвращает True при успехе."""
    code = TUYA_DP_PREFIX + str(channel)
    body = {"commands": [{"code": code, "value": bool(state)}]}
    r = _tuya_call("POST", "/v1.0/devices/%s/commands" % TUYA_DEVICE_ID,
                   body=body, token=tuya_token())
    if not r.get("success"):
        raise RuntimeError("command: %s" % r.get("msg", r))
    return bool(r.get("result"))


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

    def _handle_tuya(self):
        """POST /tuya: {"channel": 1, "state": "on|off|toggle"}."""
        t0 = time.time()

        token = self.headers.get("X-Auth", "")
        if not AUTH_TOKEN or not hmac.compare_digest(token, AUTH_TOKEN):
            self.log_message("отказ: неверный X-Auth")
            return self._json(401, {"error": "unauthorized"})

        if not (TUYA_CLIENT_ID and TUYA_SECRET and TUYA_DEVICE_ID):
            return self._json(503, {"error": "tuya not configured"})

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return self._json(400, {"error": "bad content-length"})
        if length <= 0 or length > 4096:
            return self._json(400, {"error": "bad body"})
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8", "replace"))
        except ValueError:
            return self._json(400, {"error": "bad json"})

        channel_raw = req.get("channel", 1)
        # Канал 0 или "all" — все каналы сразу («весь свет»).
        all_channels = str(channel_raw).strip().lower() in ("all", "0")
        if all_channels:
            targets = list(TUYA_CHANNELS)
            if not targets:
                return self._json(400, {"error": "no channels configured"})
        else:
            try:
                channel = int(channel_raw)
            except (TypeError, ValueError):
                return self._json(400, {"error": "bad channel"})
            if channel < 1 or channel > 8:
                return self._json(400, {"error": "channel out of range"})
            targets = [channel]

        state = str(req.get("state", "")).lower()
        try:
            if state in ("on", "off"):
                want = (state == "on")
            elif state == "toggle":
                if len(targets) == 1:
                    cur = tuya_channel_state(targets[0])
                    want = (not bool(cur)) if cur is not None else True
                else:
                    # Все сразу: если хоть один горит — гасим все, иначе включаем.
                    cur = [tuya_channel_state(c) for c in targets]
                    want = not any(bool(x) for x in cur)
            else:
                return self._json(400, {"error": "state must be on/off/toggle"})

            for c in targets:
                tuya_command(c, want)
        except Exception as e:  # noqa: BLE001 — сеть и облако бывают разными
            self.log_message("Tuya: %s", str(e)[:160])
            return self._json(502, {"error": str(e)[:160]})

        ms = int((time.time() - t0) * 1000)
        result = "on" if want else "off"
        self.log_message("Tuya каналы %s -> %s (%d мс)",
                         ",".join(str(c) for c in targets), result, ms)
        return self._json(200, {"ok": True, "channels": targets,
                                "state": result})

    def do_POST(self):  # noqa: N802 — имя задано базовым классом
        t0 = time.time()

        if self.path.startswith("/tuya"):
            return self._handle_tuya()

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
    if TUYA_CLIENT_ID and TUYA_SECRET and TUYA_DEVICE_ID:
        sys.stdout.write(
            "[proxy] Tuya: регион %s, устройство %s..., каналы %sN\n"
            % (TUYA_REGION, TUYA_DEVICE_ID[:8], TUYA_DP_PREFIX))
    else:
        sys.stdout.write("[proxy] Tuya не настроена — /tuya вернёт 503\n")
    sys.stdout.flush()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
