#!/usr/bin/env bash
# Компилирует и запускает все тесты из test/test_*.cpp обычным clang++.
# Не требует PlatformIO и сети.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-clang++}"
FAILED=0
RAN=0

# Сборка внутри репозитория: системный mktemp может быть недоступен
# в ограниченном окружении.
BUILD="$ROOT/.build"
mkdir -p "$BUILD"

for src in "$ROOT"/test/test_*.cpp; do
  [ -e "$src" ] || continue
  name="$(basename "$src" .cpp)"
  out="$BUILD/$name"
  if ! "$CXX" -std=c++17 -Wall -Wextra -I"$ROOT/src/pure" -I"$ROOT/test" \
        -o "$out" "$src"; then
    echo "СБОРКА ПРОВАЛЕНА: $name"
    FAILED=1
    rc=1
  else
    "$out"
    rc=$?
  fi
  RAN=$((RAN + 1))
  [ "$rc" -eq 0 ] || FAILED=1
  rm -f "$out"
done
rmdir "$BUILD" 2>/dev/null || true

# Вторая проверка — другим компилятором. На CI стоит clang с libstdc++,
# у которого заголовки тянут за собой меньше, чем у libc++ на Mac: так
# ловится забытый #include (например, strlen без <cstring>), который
# локально проходит, а на CI валит сборку.
if [ -z "${CXX:-}" ] && command -v g++ >/dev/null 2>&1; then
  PORT_FAILED=0
  for src in "$ROOT"/test/test_*.cpp; do
    [ -e "$src" ] || continue
    if ! g++ -std=c++17 -Wall -Wextra -fsyntax-only \
          -I"$ROOT/src/pure" -I"$ROOT/test" "$src"; then
      echo "НЕ СОБИРАЕТСЯ g++: $(basename "$src")"
      PORT_FAILED=1
    fi
  done
  if [ "$PORT_FAILED" -eq 1 ]; then
    echo "Есть модули, которые не собираются другим компилятором."
    exit 1
  fi
  echo "Проверка вторым компилятором пройдена."
fi

if [ "$RAN" -eq 0 ]; then
  echo "Тестов не найдено."
  exit 1
fi

if [ "$FAILED" -eq 0 ]; then
  echo "Все тесты прошли ($RAN)."
  exit 0
fi
echo "Есть провалившиеся тесты."
exit 1
