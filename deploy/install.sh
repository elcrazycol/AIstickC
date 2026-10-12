#!/usr/bin/env bash
# Установка STT-прокси на VPS. Выполняется НА СЕРВЕРЕ.
# Запускается через deploy/push.sh, но можно и вручную:
#   bash install.sh <домен> [порт]
#
# Принцип: только добавлять, ничего не менять. На сервере уже работает
# VLESS-VPN (порты 443/2222, Docker) — он не затрагивается.
#
# Ключ Groq спрашивается интерактивно (read -s) и попадает только в
# /etc/stt-proxy.env с правами 600.
set -euo pipefail

DOMAIN="${1:-}"
PORT="${2:-9443}"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -z "$DOMAIN" ]; then
  echo "Укажи домен: bash install.sh your.domain 9443" >&2
  exit 1
fi

echo "=== Проверка, что домен указывает на этот сервер ==="
MY_IP="$(curl -s --max-time 8 https://api.ipify.org || true)"
RESOLVED="$(getent hosts "$DOMAIN" | awk '{print $1}' | head -1 || true)"
echo "  IP сервера:  ${MY_IP:-не определить}"
echo "  $DOMAIN -> ${RESOLVED:-не разрешается}"
if [ -n "$MY_IP" ] && [ "$RESOLVED" != "$MY_IP" ]; then
  echo "  ВНИМАНИЕ: домен указывает не на этот сервер." >&2
  echo "  Сертификат не подойдёт. Продолжить? Ctrl-C для отмены." >&2
  sleep 5
fi

echo "=== Проверка, что порт $PORT свободен ==="
if ss -tlnH "sport = :$PORT" 2>/dev/null | grep -q .; then
  echo "  ПОРТ $PORT ЗАНЯТ:" >&2
  ss -tlnpH "sport = :$PORT" >&2
  echo "  Выбери другой: bash install.sh $DOMAIN 9444" >&2
  exit 1
fi
echo "  свободен"

echo "=== Проверка сертификата ==="
CERT_DIR="/etc/letsencrypt/live/$DOMAIN"
if [ ! -d "$CERT_DIR" ]; then
  echo "  нет сертификата для $DOMAIN. Есть:" >&2
  ls /etc/letsencrypt/live/ >&2
  exit 1
fi
echo "  найден $CERT_DIR"

echo "=== Python ==="
PY="$(command -v python3 || true)"
if [ -z "$PY" ]; then
  echo "  python3 не найден" >&2
  exit 1
fi
echo "  $PY ($("$PY" --version 2>&1))"
"$PY" -c "import ssl, http.server, urllib.request, json" \
  || { echo "  нет нужных модулей stdlib" >&2; exit 1; }

echo "=== Ключ Groq ==="
read -rsp "  вставь GROQ_API_KEY (gsk_...) и нажми Enter: " GROQ_KEY
echo
if [ -z "$GROQ_KEY" ]; then
  echo "  ключ пуст — выходим" >&2
  exit 1
fi
case "$GROQ_KEY" in
  gsk_*) ;;
  *) echo "  ВНИМАНИЕ: ключ не начинается с gsk_ — похоже, это не ключ Groq." >&2 ;;
esac

echo "=== Генерация AUTH_TOKEN ==="
AUTH_TOKEN="$(openssl rand -hex 24)"
echo "  сгенерирован (48 символов)"

echo "=== Пользователь ==="
if getent passwd sttproxy >/dev/null; then
  echo "  sttproxy уже есть"
else
  useradd --system --no-create-home --shell /usr/sbin/nologin sttproxy
  echo "  sttproxy создан"
fi

echo "=== Раскладка файлов ==="
mkdir -p /opt/stt-proxy /etc/stt-proxy
cp "$SRC_DIR/stt-proxy.py" /opt/stt-proxy/stt-proxy.py
chown -R root:sttproxy /opt/stt-proxy
chmod 750 /opt/stt-proxy
chmod 640 /opt/stt-proxy/stt-proxy.py

# Сертификат КОПИРУЕМ к себе, а не меняем права /etc/letsencrypt: иначе
# ослабили бы защиту ключа для чужих сервисов, а certbot при продлении
# мог бы вернуть права назад и сломать нас.
cp "$CERT_DIR/fullchain.pem" /etc/stt-proxy/cert.pem
cp "$CERT_DIR/privkey.pem"   /etc/stt-proxy/key.pem
# Владельца меняем и у КАТАЛОГА: без бита x для группы sttproxy не сможет
# войти в него и не увидит файлы, хотя сами файлы ему читаемы.
chown root:sttproxy /etc/stt-proxy
chmod 750 /etc/stt-proxy
chown root:sttproxy /etc/stt-proxy/cert.pem /etc/stt-proxy/key.pem
chmod 640 /etc/stt-proxy/cert.pem /etc/stt-proxy/key.pem
echo "  сертификат скопирован в /etc/stt-proxy/"

# Хук: после продления сертификата обновить копию и перезапуститься.
mkdir -p /etc/letsencrypt/renewal-hooks/deploy
cat > /etc/letsencrypt/renewal-hooks/deploy/stt-proxy.sh <<HOOK
#!/bin/sh
# Создан deploy/install.sh: обновляет копию сертификата после продления.
set -e
cp /etc/letsencrypt/live/$DOMAIN/fullchain.pem /etc/stt-proxy/cert.pem
cp /etc/letsencrypt/live/$DOMAIN/privkey.pem   /etc/stt-proxy/key.pem
chown root:sttproxy /etc/stt-proxy
chmod 750 /etc/stt-proxy
chown root:sttproxy /etc/stt-proxy/cert.pem /etc/stt-proxy/key.pem
chmod 640 /etc/stt-proxy/cert.pem /etc/stt-proxy/key.pem
systemctl restart stt-proxy
HOOK
chmod 755 /etc/letsencrypt/renewal-hooks/deploy/stt-proxy.sh
echo "  хук продления установлен"

echo "=== Файл окружения ==="
cat > /etc/stt-proxy.env <<ENV
GROQ_API_KEY=$GROQ_KEY
AUTH_TOKEN=$AUTH_TOKEN
PORT=$PORT
TLS_CERT=/etc/stt-proxy/cert.pem
TLS_KEY=/etc/stt-proxy/key.pem
STT_MODEL=whisper-large-v3-turbo
STT_LANG=ru
ENV
chmod 600 /etc/stt-proxy.env
echo "  /etc/stt-proxy.env создан (600)"

echo "=== systemd ==="
cp "$SRC_DIR/stt-proxy.service" /etc/systemd/system/stt-proxy.service
systemctl daemon-reload
systemctl enable --now stt-proxy
sleep 2
if ! systemctl is-active --quiet stt-proxy; then
  echo "  НЕ ЗАПУСТИЛСЯ:" >&2
  journalctl -u stt-proxy --no-pager -n 20 >&2
  exit 1
fi
echo "  активен"

echo "=== Проверка ==="
echo -n "  порт слушается: "
ss -tlnH "sport = :$PORT" | head -1 || echo "НЕТ"
echo -n "  без токена (ждём 401): "
curl -sk -o /dev/null -w "%{http_code}\n" -X POST "https://127.0.0.1:$PORT/stt" \
  --data-binary "xxxx" || echo "нет ответа"
echo -n "  с токеном, мало данных (ждём 400): "
curl -sk -o /dev/null -w "%{http_code}\n" -X POST "https://127.0.0.1:$PORT/stt" \
  -H "X-Auth: $AUTH_TOKEN" --data-binary "xxxx" || echo "нет ответа"

echo
echo "================================================================"
echo "ГОТОВО. Впиши это в include/config.h на стике:"
echo
echo "  #define PROXY_HOST \"$DOMAIN\""
echo "  #define PROXY_PORT $PORT"
echo "  #define PROXY_PATH \"/stt\""
echo "  #define PROXY_TOKEN \"$AUTH_TOKEN\""
echo
echo "Проверка с локальной машины (подставь свой путь к записи):"
echo "  tail -c +45 test.wav > test.lpcm"
echo "  curl -sk -X POST https://$DOMAIN:$PORT/stt \\"
echo "    -H \"X-Auth: $AUTH_TOKEN\" --data-binary @test.lpcm"
echo "================================================================"
