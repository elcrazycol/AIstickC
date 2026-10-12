#!/usr/bin/env bash
# Сквозная проверка: запись -> прокси -> Groq.
#
# Токен берётся с сервера, поэтому в командную строку и в историю
# он не попадает. Ключ Groq вообще не покидает сервер.
#
# Использование:
#   bash deploy/test-e2e.sh [путь-к-wav] [домен] [порт]
set -u

WAV="${1:-$(cd "$(dirname "$0")/.." && pwd)/test.wav}"
DOMAIN="${2:-your.domain}"
PORT="${3:-9443}"
HOST="${VPS:-root@your.server}"
TMP="${TMPDIR:-/tmp}"

if [ ! -f "$WAV" ]; then
  echo "нет файла $WAV" >&2
  echo "Сначала запиши пробу: python3 tools/capture.py test.wav" >&2
  exit 1
fi

echo "=== Готовим LPCM (отрезаем 44-байтный WAV-заголовок) ==="
tail -c +45 "$WAV" > "$TMP/e2e.lpcm"
SIZE="$(wc -c < "$TMP/e2e.lpcm" | tr -d ' ')"
echo "  $WAV -> $TMP/e2e.lpcm ($SIZE байт)"
DUR=$(( SIZE / 32000 ))
echo "  длительность: ~${DUR} с"

echo "=== Забираем токен с сервера (не печатаем) ==="
TOKEN="$(ssh -o ConnectTimeout=10 "$HOST" \
  "grep -m1 '^AUTH_TOKEN=' /etc/stt-proxy.env | cut -d= -f2-")"
if [ -z "$TOKEN" ]; then
  echo "  не удалось получить токен" >&2
  exit 1
fi
echo "  токен получен (${#TOKEN} символов)"

echo "=== Проверка, что сервер видит Groq ==="
ssh -o ConnectTimeout=10 "$HOST" \
  'curl -s -o /dev/null -w "  api.groq.com: HTTP %{http_code}\n" --max-time 10 https://api.groq.com/openai/v1/models -H "Authorization: Bearer $(grep -m1 "^GROQ_API_KEY=" /etc/stt-proxy.env | cut -d= -f2-)"'

echo "=== Запрос через прокси ==="
RESP="$(curl -sk --max-time 60 -X POST "https://$DOMAIN:$PORT/stt" \
  -H "X-Auth: $TOKEN" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$TMP/e2e.lpcm" \
  -w '\n--- HTTP %{http_code}, %{time_total}s ---')"
echo "$RESP"

echo
echo "=== Логи прокси ==="
ssh -o ConnectTimeout=10 "$HOST" \
  'journalctl -u stt-proxy --no-pager -n 8 | tail -8'

rm -f "$TMP/e2e.lpcm"
