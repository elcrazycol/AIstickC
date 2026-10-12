#!/usr/bin/env python3
"""PNG -> заголовок с картинкой для M5GFX.

    python3 tools/make_image.py --in konata.png --out include/idle_image.h

Два формата:
  rgb565 (по умолчанию) — полный цвет, 2 байта на пиксель, рисуется
      pushImage как lgfx::rgb565_t (без вопросов порядка байт);
  bw — 1 бит на пиксель, полутоновый дизеринг, рисуется drawBitmap.

Картинка лежит во флеше и RAM не занимает. Прозрачность композитится на
фон (--bg).
"""
import argparse
import sys

from PIL import Image

Image.MAX_IMAGE_PIXELS = 64 * 1024 * 1024


def pack_bw(img, w, h):
    bw = img.convert("1", dither=Image.Dither.FLOYDSTEINBERG)
    px = bw.load()
    bits = []
    for y in range(h):
        for x0 in range(0, w, 8):
            byte = 0
            for b in range(8):
                x = x0 + b
                if x < w and px[x, y]:
                    byte |= 0x80 >> b
            bits.append(byte)
    return bits, sum(bin(b).count("1") for b in bits), "0x%02X"


def pack_rgb565(img, w, h):
    px = img.convert("RGB").load()
    vals = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            vals.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return vals, 0, "0x%04X"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--in", dest="src", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--format", choices=("rgb565", "bw"), default="rgb565")
    ap.add_argument("--height", type=int, default=135)
    ap.add_argument("--max-width", type=int, default=240)
    ap.add_argument("--name", default="idle")
    ap.add_argument("--bg", default="black", help="фон под прозрачность")
    ap.add_argument("--invert", action="store_true",
                    help="только для bw: для картинок на светлом фоне")
    args = ap.parse_args()

    img = Image.open(args.src)
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    bg = (255, 255, 255, 255) if args.bg == "white" else (0, 0, 0, 255)
    flat = Image.new("RGBA", img.size, bg)
    flat.alpha_composite(img)
    img = flat.convert("RGB")

    w0, h0 = img.size
    if w0 < 1 or h0 < 1:
        print("ОШИБКА: пустая картинка", file=sys.stderr)
        return 1

    h = args.height
    w = max(1, round(w0 * h / h0))
    if w > args.max_width:
        w = args.max_width
        h = max(1, round(h0 * w / w0))
    img = img.resize((w, h), Image.LANCZOS)

    if args.format == "bw":
        gray = img.convert("L")
        if args.invert:
            gray = Image.eval(gray, lambda v: 255 - v)
        data, on, hexfmt = pack_bw(gray, w, h)
        ctype = "uint8_t"
        total = w * h
        note = f"точек {on}/{total} ({100 * on // max(total, 1)}%)"
    else:
        data, _, hexfmt = pack_rgb565(img, w, h)
        ctype = "uint16_t"
        note = "полный цвет"

    def fmt(vals, per_line=12):
        lines = []
        for i in range(0, len(vals), per_line):
            lines.append("  " + ", ".join(hexfmt % v for v in vals[i:i + per_line]) + ",")
        return "\n".join(lines)

    name = args.name
    with open(args.out, "w") as f:
        f.write(f"""// Сгенерировано tools/make_image.py — не редактировать вручную.
// Формат: {args.format}. {note}.
#pragma once

#include <cstdint>

static constexpr int kImage_{name}_Width = {w};
static constexpr int kImage_{name}_Height = {h};

static const {ctype} kImage_{name}_Bits[] = {{
{fmt(data)}
}};
""")

    print(f"картинка: {w}x{h}, {note}, байт {len(data) * (2 if ctype == 'uint16_t' else 1)}")
    print(f"записано: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
