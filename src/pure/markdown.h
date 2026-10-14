// Разбор markdown из ответа модели.
//
// Модель отвечает разметкой, когда вопрос её провоцирует: просят код — идёт
// блок ```, просят список — идут звёздочки. На экране 240 px это выглядит
// мусором, поэтому разметку разбираем: маркеры выделения убираем, а строки
// кода помечаем — их рисуем другим шрифтом на подложке.
//
// Строки остаются строками: перенос по ширине делает вызывающий, и пометка
// «код» переезжает на каждый получившийся кусок.
//
// Модуль чистый: ни Arduino, ни экрана, проверяется обычными тестами.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace md {

struct Line {
  std::string text;
  bool code = false;  // строка из блока кода

  Line() = default;
  Line(const std::string& t, bool c = false) : text(t), code(c) {}
};

namespace detail {

// Буква, цифра или начало UTF-8-символа. Нужно, чтобы не тронуть подчёрки-
// вание внутри слова (snake_case), но убрать его как маркер курсива.
inline bool isAlnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
}

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

inline std::string rtrim(const std::string& s) {
  size_t e = s.size();
  while (e > 0 && isSpace(s[e - 1])) --e;
  return s.substr(0, e);
}

// Убирает маркеры выделения: **жирный**, *косой*, `код`, __жирный__,
// _косой_. Подчёркивание внутри слова остаётся — это не курсив, а имя.
inline std::string stripInline(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (c == '*' || c == '`') continue;  // любые звёздочки и кавычки кода
    if (c == '_') {
      const bool left = !out.empty() && isAlnum(out.back());
      const bool right = i + 1 < in.size() && isAlnum(in[i + 1]);
      if (!(left && right)) continue;  // маркер курсива, а не часть слова
    }
    out.push_back(c);
  }
  return out;
}

// Строка из трёх обратных кавычек — граница блока кода.
inline bool isFence(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  return s.compare(i, 3, "```") == 0;
}

}  // namespace detail

// Разбирает ответ в строки: блоки кода помечаются, маркеры убираются,
// заголовки и пункты списка приводятся к простому виду.
inline void parse(const std::string& raw, std::vector<Line>& out) {
  out.clear();
  bool inCode = false;

  size_t i = 0;
  while (i <= raw.size()) {
    size_t nl = raw.find('\n', i);
    if (nl == std::string::npos) nl = raw.size();
    std::string line = raw.substr(i, nl - i);
    i = nl + 1;

    if (detail::isFence(line)) {
      inCode = !inCode;  // ``` открывает и закрывает блок
      continue;
    }

    if (inCode) {
      out.push_back({detail::rtrim(line), true});
      continue;
    }

    // Заголовок: решётки в начале строки не нужны.
    size_t h = 0;
    while (h < line.size() && line[h] == '#') ++h;
    if (h > 0 && h < line.size() && line[h] == ' ') line = line.substr(h + 1);

    // Пункт списка: звёздочку или плюс заменяем на дефис — точки в шрифте нет.
    size_t sp = 0;
    while (sp < line.size() && detail::isSpace(line[sp])) ++sp;
    if (sp + 1 < line.size() && (line[sp] == '*' || line[sp] == '+') &&
        line[sp + 1] == ' ') {
      line = line.substr(0, sp) + "-" + line.substr(sp + 1);
    }

    out.push_back({detail::stripInline(line), false});
  }

  // Пустая строка в конце не нужна.
  while (!out.empty() && out.back().text.empty() && !out.back().code) {
    out.pop_back();
  }
}

}  // namespace md
