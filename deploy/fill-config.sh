#!/usr/bin/env bash
# Заполняет include/config.h значениями прокси, забирая токен с сервера.
#
# Токен не печатается и не попадает в командную строку: он читается по ssh
# и сразу пишется в файл.
#
# Использование:
#   bash deploy/fill-config.sh [домен] [порт]
set -euo pipefail

DOMAIN="${1:-your.domain}"
PORT="${2:-9443}"
HOST="${VPS:-root@your.server}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF="$ROOT/include/config.h"

if [ ! -f "$CONF" ]; then
  echo "нет $CONF — скопируй из config.h.example" >&2
  exit 1
fi

echo "=== Забираю токен с сервера ==="
TOKEN="$(ssh -o ConnectTimeout=10 "$HOST" \
  "grep -m1 '^AUTH_TOKEN=' /etc/stt-proxy.env | cut -d= -f2-")"
if [ -z "$TOKEN" ]; then
  echo "  токен не получен" >&2
  exit 1
fi
echo "  получен (${#TOKEN} символов)"

echo "=== Обновляю config.h ==="
TOKEN="$TOKEN" DOMAIN="$DOMAIN" PORT="$PORT" python3 - "$CONF" <<'PY'
import os, re, sys

path = sys.argv[1]
tok = os.environ["TOKEN"]
domain = os.environ["DOMAIN"]
port = os.environ["PORT"]

s = open(path, encoding="utf-8").read()
def setdef(name, value):
    global s
    pat = r'#define\s+%s\s+"[^"]*"' % name
    if re.search(pat, s):
        s = re.sub(pat, '#define %s "%s"' % (name, value), s)
    else:
        s = re.sub(r'#define\s+%s\s+\S+' % name,
                   '#define %s %s' % (name, value), s)

setdef("PROXY_HOST", domain)
setdef("PROXY_PORT", port)
setdef("PROXY_PATH", "/stt")
setdef("PROXY_TOKEN", tok)
open(path, "w", encoding="utf-8").write(s)

print("  записано")
for m in re.finditer(r'#define\s+(\w+)\s+(?:"([^"]*)"|(\S+))', s):
    k = m.group(1)
    v = m.group(2) if m.group(2) is not None else m.group(3)
    if k in ("PROXY_HOST", "PROXY_PORT", "PROXY_PATH"):
        shown = v
    else:
        shown = "пусто" if not v else "заполнен (%d симв.)" % len(v)
    print("    %-16s %s" % (k, shown))
PY

echo
echo "Дальше:"
echo "  pio run -e esp32dev -t upload"
