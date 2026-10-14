// Подрезка незаконченного хвоста ответа.
//
// Модель пишет до потолка токенов и обрывается на полуслове: «…популярен для
// сервер». Показывать такое стыдно, поэтому если ответ оборван, мы отрезаем
// недописанную фразу до последнего конца предложения и ставим многоточие —
// видно, что мысль была длиннее, но читается законченно.
//
// Модуль чистый: ни Arduino, ни сети — проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <string>

namespace trim {

namespace detail {

inline bool isDigitByte(unsigned char c) { return c >= '0' && c <= '9'; }

// «Содержательный» байт: буква, цифра или начало UTF-8-символа. Нужен, чтобы
// понять, продолжается ли фраза после найденного знака.
inline bool isContentByte(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') || c >= 0x80;
}

// Внутри незакрытого примера кода резать нельзя: обрывок «func main() { fmt.»
// выглядит хуже, чем ответ вообще без примера. Считаем незакрытые фигурные
// скобки и незакрытую тройную кавычку кода.
inline bool insideCode(const std::string& t, size_t upTo) {
  int braces = 0;
  bool fence = false;
  size_t i = 0;
  while (i < upTo && i < t.size()) {
    if (t[i] == '`' && i + 2 < t.size() && t[i + 1] == '`' && t[i + 2] == '`') {
      fence = !fence;  // тройная кавычка — один переключатель, а не три
      i += 3;
      continue;
    }
    if (!fence) {
      if (t[i] == '{') {
        ++braces;
      } else if (t[i] == '}' && braces > 0) {
        --braces;
      }
    }
    ++i;
  }
  return braces > 0 || fence;
}

}  // namespace detail

// Длина, до которой надо обрезать текст (0 — конца предложения не нашлось).
//
// Концом считается последний знак . ! ? или многоточие … . Точка между
// цифрами (3.14) концом предложения не считается — это дробь.
//
// Точку внутри сокращения («и т.д.») от настоящего конца не отличить, но
// резать по ней безобидно: ответ всё равно читается законченным.
inline size_t sentenceEnd(const std::string& t) {
  for (size_t i = t.size(); i > 0; --i) {
    const size_t p = i - 1;
    const unsigned char c = (unsigned char)t[p];

    // Многоточие «…» — три байта E2 80 A6, идём с конца и видим A6 первым.
    if (c == 0xA6 && p >= 2 && (unsigned char)t[p - 2] == 0xE2 &&
        (unsigned char)t[p - 1] == 0x80) {
      return p + 1;
    }
    if (c == '.' || c == '!' || c == '?') {
      if (c == '.' && p > 0 && p + 1 < t.size() &&
          detail::isDigitByte((unsigned char)t[p - 1]) &&
          detail::isDigitByte((unsigned char)t[p + 1])) {
        continue;  // десятичная дробь, а не конец предложения
      }
      if (detail::insideCode(t, p + 1)) continue;  // обрывок примера кода
      return p + 1;
    }
    // Закрывающая скобка кода: пример закончился, резать здесь можно.
    if (c == '}' && !detail::insideCode(t, p + 1)) {
      return p + 1;
    }
    // Закрывающая кавычка примера: до неё мы были внутри кода, после — снаружи.
    if (c == '`' && p >= 2 && t[p - 1] == '`' && t[p - 2] == '`' &&
        detail::insideCode(t, p - 2)) {
      return p + 1;
    }
  }
  return 0;
}

// Обрезает незаконченный хвост. true — что-то убрали.
//
// Если конец предложения найден, текст просто заканчивается на нём: точка
// и так выглядит законченно, лишнее многоточие только мешало бы. Многоточие
// ставим лишь когда концов предложения нет вовсе и пришлось резать по
// пробелу — там обрубок виден и без пометки.
//
// Совсем короткий текст не трогаем: испортить готовый ответ хуже, чем
// оставить как есть.
inline bool trimToSentence(std::string& t) {
  size_t keep = sentenceEnd(t);
  bool marked = false;
  if (keep == 0) {
    const size_t sp = t.find_last_of(' ');
    if (sp == std::string::npos || sp < 20) return false;
    keep = sp;
    marked = true;
  }

  size_t end = keep;
  while (end > 0 &&
         (t[end - 1] == ' ' || t[end - 1] == '\n' || t[end - 1] == '\r')) {
    --end;
  }
  if (end == t.size()) return false;  // резать нечего

  t.resize(end);
  if (marked) t += "…";
  return true;
}

}  // namespace trim
