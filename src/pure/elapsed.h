// Сколько уже ждём — числом для экрана.
//
// Пока идёт вопрос, на экране должны идти точки. Но если ждать приходится
// долго, точки не говорят ничего: непонятно, работает стик или завис. Счётчик
// секунд отвечает на этот вопрос без слов.
//
// Модуль чистый: ни Arduino, ни экрана, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace elapsed {

// Секунды в короткую строку: «12 с», «1:05», «12:34».
// До минуты — просто секунды: там важен каждый тик. Дальше — как на часах.
inline void format(uint32_t ms, char* out, size_t n) {
  const uint32_t sec = ms / 1000;
  if (sec < 60) {
    snprintf(out, n, "%u с", (unsigned)sec);
  } else {
    snprintf(out, n, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
  }
}

}  // namespace elapsed
