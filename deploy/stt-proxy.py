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
import concurrent.futures
import urllib.parse
import urllib.request
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


# --- поиск в интернете ---
#
# Связка: локальный SearXNG даёт выдачу, а страницы топ-результатов читаются
# параллельно через r.jina.ai — он отдаёт чистый Markdown, поэтому парсить
# HTML на нашей стороне не нужно. Модель получает материал за один вызов
# инструмента и не должна ходить читать ссылки вторым шагом.
SEARX_URL = "http://127.0.0.1:8888/search"
JINA_BASE = "https://r.jina.ai/"
JINA_KEY = os.environ.get("JINA_KEY", "")   # необязательный: 200 RPM вместо 20
SEARCH_RESULTS = 5        # сколько результатов показываем модели
SEARCH_PAGES = 3          # сколько из них вычитываем целиком
SEARCH_PAGE_CHARS = 2000  # сколько знаков берём со страницы
SEARX_TIMEOUT = 6
JINA_TIMEOUT = 7
# Jina рендерит страницу браузером, и без ограничения это занимает по пять
# секунд на сайт. X-Timeout говорит ей отдать то, что успела, — замеры:
# 0.5 с на лёгкой странице, 3.8 с на Википедии, и всё равно с содержимым.
JINA_RENDER_TIMEOUT = "2.5"
# Сколько ждём страницы суммарно. Замеры показали разброс от 1.3 до 6.8 с:
# всё решает самая медленная страница. Ждать её дольше четырёх секунд
# незачем — кто не успел, пойдёт в ответ сниппетом из выдачи.
SEARCH_BUDGET = 4.0


def http_get(url, timeout, headers=None):
    req = urllib.request.Request(url, headers=headers or {})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def searx_search(query):
    """Выдача локального SearXNG в виде списка словарей."""
    url = SEARX_URL + "?" + urllib.parse.urlencode({
        "q": query, "format": "json", "language": "ru-RU", "safesearch": 0,
    })
    data = json.loads(http_get(url, SEARX_TIMEOUT).decode("utf-8", "replace"))
    out = []
    for item in (data.get("results") or [])[:SEARCH_RESULTS]:
        out.append({
            "title": (item.get("title") or "").strip(),
            "url": (item.get("url") or "").strip(),
            "snippet": (item.get("content") or "").strip(),
        })
    return [r for r in out if r["url"]]


def trim_at_boundary(text, limit):
    """Обрезает по границе абзаца: рвать мысль на полуслове незачем."""
    if len(text) <= limit:
        return text
    cut = text[:limit]
    for sep in ("\n\n", "\n", ". ", " "):
        pos = cut.rfind(sep)
        if pos > limit // 2:
            return cut[:pos].rstrip() + " …"
    return cut.rstrip() + " …"


def jina_read(url):
    """Чистый Markdown страницы. Пустая строка — не получилось."""
    headers = {
        "X-Retain-Images": "none",      # ссылки на картинки — лишние токены
        "X-Return-Format": "markdown",
        "X-Timeout": JINA_RENDER_TIMEOUT,
    }
    if JINA_KEY:
        headers["Authorization"] = "Bearer " + JINA_KEY
    try:
        raw = http_get(JINA_BASE + url, JINA_TIMEOUT, headers)
    except Exception as exc:  # noqa: BLE001 — сайт мог не отдаться, это норма
        print("jina %s: %s" % (url, exc), flush=True)
        return ""
    return trim_at_boundary(raw.decode("utf-8", "replace").strip(),
                            SEARCH_PAGE_CHARS)


def plural(n, one, few, many):
    """Русское склонение: 1 источник, 2 источника, 5 источников."""
    if n % 10 == 1 and n % 100 != 11:
        return one
    if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
        return few
    return many


def read_chunked(rfile, limit):
    """Читает тело в кодировке chunked.

    Так стик шлёт звук прямо во время записи: куски уходят по мере того, как
    человек говорит, а не одной пачкой после отпускания кнопки. Значит, к
    моменту окончания записи почти всё уже здесь.

    Возвращает (данные, ошибка).
    """
    parts = []
    total = 0
    while True:
        line = rfile.readline()
        if not line:
            return b"", "обрыв до конца тела"
        line = line.strip()
        if not line:
            continue
        try:
            size = int(line.split(b";")[0], 16)
        except ValueError:
            return b"", "плохой размер куска"
        if size == 0:
            rfile.readline()  # завершающий CRLF
            break
        total += size
        if total > limit:
            return b"", "audio too large"
        parts.append(rfile.read(size))
        rfile.readline()  # CRLF после данных
    return b"".join(parts), None


def _ulaw_table():
    """Таблица разворота µ-law в 16-битные отсчёты (G.711).

    audioop из стандартной библиотеки в новых Python удалён, а таблица
    считается один раз при старте и работает без зависимостей.
    """
    table = []
    for code in range(256):
        u = ~code & 0xFF
        t = ((u & 0x0F) << 3) + 0x84
        t <<= (u & 0x70) >> 4
        table.append((0x84 - t) if (u & 0x80) else (t - 0x84))
    return table


_ULAW = _ulaw_table()


def ulaw_to_pcm(data):
    """8-битный µ-law -> 16-битный PCM, моно, little-endian."""
    out = bytearray(len(data) * 2)
    for i, code in enumerate(data):
        v = _ULAW[code]
        out[i * 2] = v & 0xFF
        out[i * 2 + 1] = (v >> 8) & 0xFF
    return bytes(out)


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

    def _handle_search(self):
        """POST /search: тело — сам поисковый запрос сырым текстом.

        Сырым, а не в JSON: в языке скриптов нет экранирования, и собирать
        из запроса JSON было бы ненадёжно.
        """
        t0 = time.time()
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return self._json(400, {"error": "bad content-length"})
        if length <= 0 or length > 1024:
            self.log_message("поиск: плохая длина запроса %d", length)
            return self._json(400, {"error": "bad query"})

        query = self.rfile.read(length).decode("utf-8", "replace").strip()
        if not query:
            return self._json(400, {"error": "empty query"})

        try:
            results = searx_search(query)
        except Exception as exc:  # noqa: BLE001
            self.log_message("поиск не удался: %s", exc)
            return self._json(502, {"error": "searxng: %s" % exc})

        if not results:
            return self._json(200, {"text": "По запросу ничего не нашлось."})

        # Страницы читаем параллельно: по очереди это заняло бы секунды,
        # а так — сколько идёт самая медленная из них.
        pages = [""] * len(results)
        pool = concurrent.futures.ThreadPoolExecutor(SEARCH_PAGES)
        jobs = {pool.submit(jina_read, r["url"]): i
                for i, r in enumerate(results[:SEARCH_PAGES])}
        try:
            for job in concurrent.futures.as_completed(jobs, timeout=SEARCH_BUDGET):
                pages[jobs[job]] = job.result()
        except concurrent.futures.TimeoutError:
            # Кто не успел — обойдёмся сниппетом из выдачи: лучше быстрый
            # ответ с двумя страницами, чем медленный с тремя.
            pass
        # Оставшиеся потоки не ждём: у запросов свои таймауты.
        pool.shutdown(wait=False, cancel_futures=True)

        parts = ["Результаты поиска по запросу «%s»:" % query]
        for i, r in enumerate(results):
            parts.append("\n### %d. %s\n%s" % (i + 1, r["title"], r["url"]))
            if r["snippet"]:
                parts.append("Из выдачи: %s" % r["snippet"])
            if pages[i]:
                parts.append("Со страницы:\n%s" % pages[i])
        text = "\n".join(parts)

        # Короткий факт для строки над ответом: что реально произошло.
        # Стик покажет его над ответом модели, чтобы было видно, что ответ
        # пришёл из интернета и насколько глубоко он копал.
        # Строка тела держит 29 знаков, поэтому пометка короткая: сколько
        # нашлось и сколько страниц реально прочитано. «Читал» — потому что
        # «прочитано 3 страницы» уже не влезает и переносится на вторую строку.
        read_pages = sum(1 for p in pages if p)
        note = "поиск: %d %s, читал %d" % (
            len(results), plural(len(results), "источник", "источника", "источников"),
            read_pages)

        self.log_message("поиск «%s»: %d результатов, %d знаков, со страниц %d "
                         "за %d мс", query, len(results), len(text),
                         read_pages, int((time.time() - t0) * 1000))
        return self._json(200, {"text": text, "note": note})

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

        # Общий секрет для всего: без него любой жёг бы квоту Groq,
        # дёргал наш SearXNG и ходил читать страницы через Jina.
        token = self.headers.get("X-Auth", "")
        if not AUTH_TOKEN or not hmac.compare_digest(token, AUTH_TOKEN):
            self.log_message("отказ: неверный X-Auth")
            return self._json(401, {"error": "unauthorized"})

        if self.path.startswith("/search"):
            return self._handle_search()
        if self.path.startswith("/tuya"):
            return self._handle_tuya()

        if not self.path.startswith("/stt"):
            return self._json(404, {"error": "not found"})

        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            pcm, err = read_chunked(self.rfile, MAX_BYTES)
            if err:
                code = 413 if err == "audio too large" else 400
                return self._json(code, {"error": err})
        else:
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

        # Стик шлёт звук в µ-law (8 бит вместо 16) — разворачиваем в PCM,
        # дальше всё как раньше. Проверка длины идёт уже по развёрнутому.
        if self.headers.get("Content-Type", "").startswith("audio/basic"):
            pcm = ulaw_to_pcm(pcm)

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
