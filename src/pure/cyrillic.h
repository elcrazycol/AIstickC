// Работа с UTF-8: посимвольный обход и оценка ширины строки.
// Ключевое свойство: обход всегда по границам кодпоинтов, поэтому
// разрезать кириллическую букву пополам структурно невозможно.
#pragma once

#include <cstdint>
#include <cstddef>

namespace cyr {

// Длина кодпоинта в байтах по его первому байту. 1..4.
inline size_t nextCodepoint(const char* s, size_t i) {
  const uint8_t c = (uint8_t)s[i];
  if (c == 0) return 0;
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;  // повреждённый байт — считаем одним, чтобы не зациклиться
}

inline size_t countCodepoints(const char* s) {
  size_t n = 0, i = 0;
  while (s[i] != 0) { i += nextCodepoint(s, i); ++n; }
  return n;
}

// Декодирование кодпоинта из позиции i. Возвращает 0xFFFD при ошибке.
inline uint32_t codepointAt(const char* s, size_t i) {
  const uint8_t c = (uint8_t)s[i];
  if (c == 0) return 0;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) {
    return (uint32_t)((c & 0x1F) << 6) | ((uint8_t)s[i + 1] & 0x3F);
  }
  if ((c & 0xF0) == 0xE0) {
    return (uint32_t)((c & 0x0F) << 12) |
           (uint32_t)(((uint8_t)s[i + 1] & 0x3F) << 6) |
           ((uint8_t)s[i + 2] & 0x3F);
  }
  if ((c & 0xF8) == 0xF0) {
    return (uint32_t)((c & 0x07) << 18) |
           (uint32_t)(((uint8_t)s[i + 1] & 0x3F) << 12) |
           (uint32_t)(((uint8_t)s[i + 2] & 0x3F) << 6) |
           ((uint8_t)s[i + 3] & 0x3F);
  }
  return 0xFFFD;
}

// Ширина одной ячейки моноширинного шрифта. ASCII занимает половину,
// всё остальное (кириллица и прочее) — целую.
inline int& cellWidthRef() { static int w = 8; return w; }
inline void setCellWidth(int px) { cellWidthRef() = px; }
inline int cellWidth() { return cellWidthRef(); }

inline int codepointWidth(uint32_t cp) {
  return (cp < 0x80) ? (cellWidth() + 1) / 2 : cellWidth();
}

inline int textWidth(const char* s) {
  int w = 0;
  size_t i = 0;
  while (s[i] != 0) {
    w += codepointWidth(codepointAt(s, i));
    i += nextCodepoint(s, i);
  }
  return w;
}

}  // namespace cyr
