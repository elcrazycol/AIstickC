// Перенос UTF-8 текста по пиксельной ширине и пагинация.
//
// Модуль чистый: ширину символа сообщает вызывающий через MeasureFn, поэтому
// он не зависит ни от шрифта, ни от Arduino. В прошивке это константа
// моноширинного шрифта, в тестах — любая удобная функция.
//
// Гарантии:
//  - строки режутся только по границам UTF-8 (посреди кириллицы — нельзя);
//  - слова переносятся целиком, слишком длинное слово рвётся посимвольно;
//  - '\n' — жёсткий перенос; пробелы подряд схлопываются в один.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace textwrap {

// Ширина одного UTF-8 символа в пикселях.
typedef int (*MeasureFn)(const char* cp, size_t len, void* ctx);

// Длина UTF-8 символа по первому байту. 0 — некорректный старший байт.
inline size_t charLen(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 0;
}

// Разбивает text на строки не шире maxWidthPx. Результат — в lines,
// прежнее содержимое очищается. '\r' игнорируется.
inline void wrap(const char* text, int maxWidthPx, MeasureFn measure,
                void* ctx, std::vector<std::string>& lines) {
  lines.clear();
  if (text == nullptr) {
    lines.push_back(std::string());
    return;
  }

  std::string cur;
  int curW = 0;
  bool haveWord = false;  // в строке уже есть слово — перед следующим пробел

  auto flush = [&]() {
    lines.push_back(cur);
    cur.clear();
    curW = 0;
    haveWord = false;
  };

  const int spaceW = measure(" ", 1, ctx);

  // Кладёт слово в текущую строку, при необходимости перенося или разрывая.
  auto placeWord = [&](const char* w, size_t len) {
    int wordW = 0;
    for (size_t k = 0; k < len;) {
      const size_t cl = charLen((unsigned char)w[k]);
      const size_t step = cl == 0 ? 1 : cl;
      wordW += measure(w + k, step, ctx);
      k += step;
    }

    // Слово длиннее всей строки — рвём посимвольно. Пробел-разделитель
    // добавляется один раз, перед первым символом слова.
    if (wordW > maxWidthPx) {
      bool first = true;
      for (size_t k = 0; k < len;) {
        const size_t cl = charLen((unsigned char)w[k]);
        const size_t step = cl == 0 ? 1 : cl;
        const int cw = measure(w + k, step, ctx);
        if (first) {
          if (haveWord && curW + spaceW + cw > maxWidthPx) flush();
          if (haveWord) {
            cur += ' ';
            curW += spaceW;
          }
          first = false;
        } else if (curW + cw > maxWidthPx) {
          flush();
        }
        cur.append(w + k, step);
        curW += cw;
        haveWord = true;
        k += step;
      }
      return;
    }

    const int sep = haveWord ? spaceW : 0;
    if (haveWord && curW + sep + wordW > maxWidthPx) flush();
    if (haveWord) {
      cur += ' ';
      curW += spaceW;
    }
    cur.append(w, len);
    curW += wordW;
    haveWord = true;
  };

  size_t i = 0;
  while (text[i] != 0) {
    const char c = text[i];
    if (c == '\n') {
      flush();
      ++i;
      continue;
    }
    if (c == ' ' || c == '\r' || c == '\t') {
      ++i;
      continue;
    }
    const size_t start = i;
    while (text[i] != 0 && text[i] != ' ' && text[i] != '\n' &&
           text[i] != '\r' && text[i] != '\t') {
      const size_t cl = charLen((unsigned char)text[i]);
      i += cl == 0 ? 1 : cl;
    }
    placeWord(text + start, i - start);
  }

  if (!cur.empty() || lines.empty()) flush();
}

// Число страниц по linesPerPage строк. Пустой текст — одна страница.
inline size_t pageCount(size_t totalLines, size_t linesPerPage) {
  if (linesPerPage == 0) return 1;
  if (totalLines == 0) return 1;
  return (totalLines + linesPerPage - 1) / linesPerPage;
}

// Границы страницы page: [begin, end). page вне диапазона зажимается.
inline void pageRange(size_t page, size_t totalLines, size_t linesPerPage,
                      size_t& begin, size_t& end) {
  if (linesPerPage == 0) linesPerPage = 1;
  const size_t total = pageCount(totalLines, linesPerPage);
  if (page >= total) page = total - 1;
  begin = page * linesPerPage;
  end = begin + linesPerPage;
  if (end > totalLines) end = totalLines;
}

}  // namespace textwrap
