#!/usr/bin/env python3
"""TTF -> заголовок GFXfont для M5GFX (ASCII + кириллица).

Запускается вручную, результат коммитится — сама сборка Python не требует.

    python3 tools/make_font.py --ttf /tmp/jbmono.ttf \
        --out include/font_jbmono16.h --size 16

Формат — Adafruit/M5GFX GFXfont: 1 бит на пиксель, строки упакованы побайтно,
старший бит — левый пиксель. ASCII и кириллица лежат в двух диапазонах
(lgfx::EncodeRange), между ними дыра не занимает память.

Шрифт фиксированной ширины, поэтому xAdvance одинаковый у всех глифов.
"""
import argparse
import sys

from PIL import Image, ImageDraw, ImageFont

# Диапазоны: (начало, конец). Порядок важен — задаёт раскладку глифов.
# Кроме ASCII и кириллицы — типографика, которой полно в ответах модели:
# «ёлочки», тире, кавычки, многоточие, номер.
RANGES = [
    (0x20, 0x7E),      # ASCII
    (0xAB, 0xBB),      # « » и соседние
    (0x400, 0x45F),    # кириллица
    (0x2010, 0x2015),  # дефисы и тире (в т.ч. — U+2014)
    (0x2018, 0x201F),  # типографские кавычки
    (0x2026, 0x2026),  # многоточие …
    (0x2116, 0x2116),  # знак номера №
]


def glyph_bitmap(ch, font, bbox, threshold=128):
    """Упаковывает один символ в битмап Adafruit/M5GFX GFXfont.

    Ключевой момент формата: биты глифа идут НЕПРЕРЫВНЫМ потоком, без
    выравнивания каждой строки к байту. M5GFX читает ровно w*h бит подряд,
    поэтому выравнивание строк ломает все глифы шире 8 px (ж, Д, Ш, W).
    К байту выравнивается только конец глифа.

    Рисуем в grayscale и порогуем: рисование сразу в 1-битную картинку
    заставляет PIL дизерить сглаживание, и глиф превращается в шум.
    """
    left, top, right, bottom = bbox
    w = right - left
    h = bottom - top
    if w <= 0 or h <= 0:
        return []
    img = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(img)
    # Сдвигаем так, чтобы чернила легли в левый верхний угол.
    d.text((-left, -top), ch, font=font, fill=255)
    px = img.load()

    out = []
    byte = 0
    nbits = 0
    for y in range(h):
        for x in range(w):
            byte = (byte << 1) | (1 if px[x, y] >= threshold else 0)
            nbits += 1
            if nbits == 8:
                out.append(byte)
                byte = 0
                nbits = 0
    if nbits:  # хвост глифа — добить нулями до байта
        out.append(byte << (8 - nbits))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=16)
    ap.add_argument("--name", default=None, help="имя массива (без kFont)")
    args = ap.parse_args()

    font = ImageFont.truetype(args.ttf, args.size)
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    ascent, descent = font.getmetrics()

    # Моноширинный: одна ширина на все глифы.
    advance = int(round(font.getlength("M")))
    if advance <= 0:
        print("ОШИБКА: не определить ширину глифа 'M'", file=sys.stderr)
        return 1

    bitmaps = []
    glyphs = []
    above = 0   # максимум чернил выше базовой линии
    below = 0   # максимум чернил ниже базовой линии

    print(f"шрифт: {args.ttf} @ {args.size}px")

    for lo, hi in RANGES:
        for cp in range(lo, hi + 1):
            ch = chr(cp)
            bbox = probe.textbbox((0, 0), ch, font=font)
            left, top, right, bottom = bbox
            w = right - left
            h = bottom - top

            # Пробел и пустые глифы: нулевая ширина, обычный шаг.
            if w <= 0 or h <= 0:
                glyphs.append((len(bitmaps), 0, 0, advance, 0, 0))
                continue

            above = max(above, ascent - top)
            below = max(below, bottom - ascent)

            data = glyph_bitmap(ch, font, bbox)
            glyphs.append((len(bitmaps), w, h, advance, left, top - ascent))
            bitmaps.extend(data)

    if not bitmaps:
        print("ОШИБКА: ни одного глифа не собралось", file=sys.stderr)
        return 1

    # Плотный межстрочный интервал по фактическим чернилам, а не по
    # огромным line-gap метрикам шрифта. +2 px запаса, чтобы строки не слипались.
    y_advance = above + below + 2

    array = args.name or f"jbmono{args.size}"
    font_name = args.ttf.rsplit("/", 1)[-1]

    def fmt_bytes(data, per_line=16):
        lines = []
        for i in range(0, len(data), per_line):
            chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + per_line])
            lines.append("  " + chunk + ",")
        return "\n".join(lines)

    def fmt_glyphs(gs):
        lines = []
        for off, w, h, adv, xo, yo in gs:
            lines.append(f"  {{ {off}, {w}, {h}, {adv}, {xo}, {yo} }},")
        return "\n".join(lines)

    ranges = ",\n".join(f"  {{ 0x{lo:04X}, 0x{hi:04X}, {base} }}"
                        for (lo, hi), base in
                        zip(RANGES, _range_bases()))

    with open(args.out, "w") as f:
        f.write(f"""// Сгенерировано tools/make_font.py — не редактировать вручную.
// {font_name}, {args.size} px: ASCII, кириллица и типографика
// (тире, кавычки, многоточие). Один бит на пиксель.
// Битмап глифа — непрерывный поток w*h бит без выравнивания строк.
// Лицензия шрифта — OFL 1.1.
#pragma once

#include <M5GFX.h>

static const uint8_t kFont_{array}_Bitmaps[] = {{
{fmt_bytes(bitmaps)}
}};

static const lgfx::GFXglyph kFont_{array}_Glyphs[] = {{
{fmt_glyphs(glyphs)}
}};

static const lgfx::EncodeRange kFont_{array}_Ranges[] = {{
{ranges}
}};

static const lgfx::GFXfont kFont_{array} = {{
  (uint8_t*)kFont_{array}_Bitmaps,
  (lgfx::GFXglyph*)kFont_{array}_Glyphs,
  0x{RANGES[0][0]:04X}, 0x{RANGES[-1][1]:04X}, {y_advance},
  {len(RANGES)}, (lgfx::EncodeRange*)kFont_{array}_Ranges,
}};

static constexpr int kFont_{array}_YAdvance = {y_advance};
static constexpr int kFont_{array}_XAdvance = {advance};
""")

    total = len(bitmaps) + len(glyphs) * 12
    print(f"метрики: ascent={ascent} descent={descent} "
          f"above={above} below={below}")
    print(f"итог: yAdvance={y_advance} xAdvance={advance}")
    print(f"глифов: {len(glyphs)}, битмап {len(bitmaps)} байт, "
          f"итого ≈{total} байт")
    print(f"записано: {args.out}")
    return 0


def _range_bases():
    """Базовый индекс в массиве глифов для каждого диапазона."""
    bases = []
    acc = 0
    for lo, hi in RANGES:
        bases.append(acc)
        acc += hi - lo + 1
    return bases


if __name__ == "__main__":
    sys.exit(main())
