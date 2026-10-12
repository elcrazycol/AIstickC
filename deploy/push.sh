#!/usr/bin/env bash
# Копирует файлы прокси на сервер и запускает установку.
#
# Ключ Groq вводится НА СЕРВЕРЕ (ssh -t), поэтому не попадает ни в
# командную строку, ни в историю, ни в ps.
#
# Использование:
#   bash deploy/push.sh <домен> [порт]
#
# Пример:
#   bash deploy/push.sh your.domain 9443
set -euo pipefail

DOMAIN="${1:-}"
PORT="${2:-9443}"
HOST="${VPS:-root@your.server}"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
REMOTE_DIR="/root/stt-deploy"

if [ -z "$DOMAIN" ]; then
  echo "Укажи домен: bash deploy/push.sh your.domain 9443" >&2
  exit 1
fi

echo "=== Проверка домена (локально) ==="
IP="$(dig +short "$DOMAIN" A 2>/dev/null | tail -1 || true)"
if [ -z "$IP" ]; then
  IP="$(getent hosts "$DOMAIN" 2>/dev/null | awk '{print $1}' | head -1 || true)"
fi
if [ -z "$IP" ]; then
  echo "  ВНИМАНИЕ: $DOMAIN не разрешается в IP." >&2
  echo "  Сертификат Let's Encrypt не подойдёт, и стик не сможет" >&2
  echo "  подключиться по этому имени. Продолжить? Ctrl-C для отмены." >&2
  sleep 5
else
  echo "  $DOMAIN -> $IP"
fi

echo "=== Копирование файлов ==="
ssh -o ConnectTimeout=10 "$HOST" "mkdir -p $REMOTE_DIR"
scp -q "$SRC_DIR/stt-proxy.py" "$SRC_DIR/stt-proxy.service" \
       "$SRC_DIR/install.sh" "$HOST:$REMOTE_DIR/"
ssh "$HOST" "chmod +x $REMOTE_DIR/install.sh"
echo "  скопировано в $REMOTE_DIR"

echo
echo "=== Установка на сервере (потребуется ключ Groq) ==="
echo "    ssh -t нужен, чтобы ввод ключа работал без эха."
echo
ssh -t "$HOST" "bash $REMOTE_DIR/install.sh $DOMAIN $PORT"
