#!/usr/bin/env python3
"""Эмулятор экрана стика: рисует PNG и GIF из настоящего шрифта и картинки.

    python3 tools/mock_screen.py            # всё в docs/images/
    python3 tools/mock_screen.py --out DIR

Шрифт (include/font_jbmono.h) и картинка (include/idle_image.h) — те же
сгенерированные файлы, что идут в прошивку, поэтому текст и портрет
пиксель-в-пиксель совпадают с устройством.

Логика отрисовки повторяет src/ui.cpp: константы раскладки и цвета продублированы
(меняешь ui.cpp — поправь и здесь). Камера для README не нужна.
"""
import argparse
import math
import os
import re
import sys

from PIL import Image

# --- раскладка: должно совпадать с src/ui.cpp и include/settings.h ---
W, H = 240, 135
STATUS_H = 14
MARGIN = 2
BODY_LINES = 5
PAGE_BASE = 8       # центр строки состояния для счётчика страниц
ASCENT = 14         # подъём шрифта (макс. -yOffset в таблице глифов)
DESCENT = 4         # ниже базовой линии
TEXT_H = ASCENT + DESCENT
WAVE_BARS = 58
WAVE_H = 30
FIRST_BASE = 26

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
RED = (255, 0, 0)
GREEN = (0, 200, 0)
YELLOW = (255, 255, 0)
GRAY = (128, 132, 128)      # 0x8410
PANEL = (24, 28, 24)        # 0x18E3 — нейтральная панель (notice)
SEL_BG = (33, 36, 66)       # 0x2128 — подложка выбранной строки списка
ORANGE = (255, 165, 0)      # TFT_ORANGE — метка «есть новее»
PANEL_EDGE = (128, 132, 128)
DIM_DARK = (33, 4, 4)       # 0x2104 (пустые палочки)
CODE_BG = (16, 20, 16)      # 0x10A2 — подложка блока кода
CODE_EDGE = (45, 48, 45)    # 0x2E68 — полоска слева
CODE_TEXT = (156, 234, 156) # 0x9F51 — мягкий зелёный

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---------------------------------------------------------------- шрифт
class Font:
    def __init__(self, path):
        src = open(path).read()
        # Байты в заголовке записаны hex-литералами — парсим именно их,
        # иначе "0x18" разбирается как 0 и 18 и глифы рассыпаются.
        self.bits = [int(x, 16) for x in re.findall(
            r"0x([0-9A-Fa-f]{2})",
            re.search(r"_Bitmaps\[\] = \{(.*?)\n\};", src, re.S).group(1))]
        self.glyphs = []
        for line in re.search(r"_Glyphs\[\] = \{(.*?)\n\};", src, re.S).group(1).splitlines():
            n = [int(x) for x in re.findall(r"-?\d+", line)]
            if len(n) == 6:
                self.glyphs.append(tuple(n))
        self.ranges = [(int(a, 16), int(b, 16), int(c)) for a, b, c in re.findall(
            r"0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), (\d+)",
            re.search(r"_Ranges\[\] = \{(.*?)\};", src, re.S).group(1))]
        self.xadv = int(re.search(r"_XAdvance = (\d+)", src).group(1))
        self.yadv = int(re.search(r"_YAdvance = (\d+)", src).group(1))

    def _glyph(self, cp):
        for lo, hi, base in self.ranges:
            if lo <= cp <= hi:
                return self.glyphs[cp - lo + base]
        return None

    def text(self, img, s, x, top, color=WHITE, size=1):
        """top — верх строки текста, как cursor_y в M5GFX (не базовая линия!).

        M5GFX рисует глиф на top + ASCENT + yOffset глифа. Проверено на
        устройстве: cursor_y=20 даёт чернила в 23..35.
        """
        px = img.load()
        for ch in s:
            g = self._glyph(ord(ch))
            if g is None:
                x += self.xadv * size
                continue
            off, w, h, adv, xo, yo = g
            for row in range(h):
                for col in range(w):
                    bit = row * w + col
                    if self.bits[off + bit // 8] & (0x80 >> (bit % 8)):
                        for dy in range(size):
                            for dx in range(size):
                                xx = x + (xo + col) * size + dx
                                yy = top + (ASCENT + yo + row) * size + dy
                                if 0 <= xx < W and 0 <= yy < H:
                                    px[xx, yy] = color
            x += adv * size
        return x

    def width(self, s, size=1):
        return len(s) * self.xadv * size

    def height(self, size=1):
        # высота строки шрифта = выше+ниже базовой линии
        above = max(-g[5] for g in self.glyphs)
        below = max(g[5] + g[2] for g in self.glyphs)
        return (above + below) * size


# ---------------------------------------------------------------- картинка
def load_image(path):
    src = open(path).read()
    w = int(re.search(r"_Width = (\d+)", src).group(1))
    h = int(re.search(r"_Height = (\d+)", src).group(1))
    vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{4}",
            re.search(r"_Bits\[\] = \{(.*?)\n\};", src, re.S).group(1))]
    img = Image.new("RGB", (w, h))
    px = img.load()
    for i, v in enumerate(vals[:w * h]):
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        px[i % w, i // w] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    return img


# ---------------------------------------------------------------- цвет
def hue565(h):
    reg = h // 43
    rem = (h - reg * 43) * 6
    tbl = [(255, rem, 0), (255 - rem, 255, 0), (0, 255, rem),
           (0, 255 - rem, 255), (rem, 0, 255), (255, 0, 255 - rem)]
    return tbl[reg % 6]


# ---------------------------------------------------------------- экраны
def fill(px, x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            px[x, y] = color


_YADV = None


def font_yadv():
    """Высота строки шрифта — нужна там, где нет объекта Font под рукой."""
    global _YADV
    if _YADV is None:
        _YADV = Font(os.path.join(ROOT, "include", "font_jbmono.h")).yadv
    return _YADV


def screen_tool(font, label, pct=78, rssi=4):
    """Экран ожидания инструмента: надпись и точки строкой ниже.

    Повторяет ui::toolWait в src/ui.cpp.
    """
    img = Image.new("RGB", (W, H), BLACK)
    status_bar(img, font, pct, rssi)
    font.text(img, label, MARGIN, FIRST_BASE, GRAY)
    draw_dots(img, 0, row=1)
    return img


def circle(px, cx, cy, r, color):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if 0 <= x < W and 0 <= y < H and (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px[x, y] = color


def ring(px, cx, cy, r, color):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d = (x - cx) ** 2 + (y - cy) ** 2
            if r * r - 2 * r <= d <= r * r and 0 <= x < W and 0 <= y < H:
                px[x, y] = color


def status_bar(img, font, pct, rssi=4):
    px = img.load()
    fill(px, 0, 0, W, STATUS_H, BLACK)
    bx, by, bw, bh = W - MARGIN - 15 - 2, 13 - 8, 15, 8
    for x in range(bx, bx + bw):
        px[x, by] = px[x, by + bh - 1] = GRAY
    for y in range(by, by + bh):
        px[bx, y] = px[bx + bw - 1, y] = GRAY
    px[bx + bw, by + 2] = px[bx + bw + 1, by + 2] = GRAY
    inner = (bw - 2) * pct // 100
    if inner > 0:
        c = RED if pct <= 20 else (YELLOW if pct <= 50 else GREEN)
        fill(px, bx + 1, by + 1, bx + 1 + inner, by + bh - 1, c)
    sx0 = bx - 8 - (4 * 2 + 3 * 2)
    for i in range(4):
        hh = 3 + i * 2
        c = GREEN if i < rssi else DIM_DARK
        fill(px, sx0 + i * 4, 13 - hh, sx0 + i * 4 + 2, 13, c)


def screen_idle(font, portrait, msgs=12, mem=2, pct=78, rssi=4):
    img = Image.new("RGB", (W, H), BLACK)
    img.paste(portrait, (W - portrait.width, H - portrait.height))
    status_bar(img, font, pct, rssi)
    font.text(img, "elcrazycol/", MARGIN, 40, GRAY)
    font.text(img, "AIstickC", MARGIN, 60, GREEN)
    font.text(img, f"{msgs} msgs", MARGIN, 80, GRAY)
    font.text(img, f"В памяти: {mem}", MARGIN, H - 4, GRAY)
    return img


def screen_recording(font, waves, remain_ms=12000, pct=78, rssi=4):
    img = Image.new("RGB", (W, H), BLACK)
    status_bar(img, font, pct, rssi)
    sec = f"{(remain_ms + 999) // 1000}"
    size = 2
    font.text(img, sec, (W - font.width(sec, size)) // 2,
              30 + (52 - font.height(size)) // 2, RED, size)
    hint = "Говори. Отпустишь — отправлю"
    font.text(img, hint, (W - font.width(hint)) // 2, 88, GRAY)
    draw_waves(img, waves)
    return img


def draw_waves(img, waves):
    px = img.load()
    bar_w, gap = 3, 1
    total = WAVE_BARS * (bar_w + gap) - gap
    x0 = (W - total) // 2
    top = H - WAVE_H - 1
    fill(px, 0, top, W, H, BLACK)
    for i, amp in enumerate(waves[:WAVE_BARS]):
        if amp <= 0:
            continue
        hh = max(1, amp * WAVE_H // 255)
        c = hue565((i * 256 // WAVE_BARS + 40) % 256)
        fill(px, x0 + i * (bar_w + gap), H - hh, x0 + i * (bar_w + gap) + bar_w, H, c)


def screen_waiting(font, phase, pct=78, rssi=4):
    img = Image.new("RGB", (W, H), BLACK)
    status_bar(img, font, pct, rssi)
    draw_dots(img, 0)
    return img


def draw_dots(img, now_ms, row=0):
    px = img.load()
    r, gap = 3, 8
    top = FIRST_BASE + row * font_yadv()
    cy = top + font_yadv() // 2
    x0 = MARGIN + 3
    fill(px, 0, top, gap * 2 + 12, top + font_yadv(), BLACK)
    phase = (now_ms % 900) / 900.0 * 2 * math.pi
    for i in range(3):
        dy = int(3 * math.sin(phase + i * 2.0944))
        c = hue565((now_ms // 5 + i * 70) % 256)
        for y in range(cy + dy - r, cy + dy + r + 1):
            for x in range(x0 + i * gap - r, x0 + i * gap + r + 1):
                if (x - (x0 + i * gap)) ** 2 + (y - (cy + dy)) ** 2 <= r * r + 1:
                    if 0 <= x < W and 0 <= y < H:
                        px[x, y] = c


def screen_answer(font, lines, page, total, pct=78, rssi=4, color=WHITE,
                  note=None):
    """Экран ответа. note — пометка инструмента: она занимает первую строку
    первой страницы и рисуется мелким серым шрифтом, как подпись.
    """
    img = Image.new("RGB", (W, H), BLACK)
    status_bar(img, font, pct, rssi)
    small = None
    px = img.load()
    for row in range(BODY_LINES):
        idx = page * BODY_LINES + row
        if note and idx == 0:
            if small is None:
                small = Font(os.path.join(ROOT, "include", "font_small.h"))
            small.text(img, note, MARGIN, FIRST_BASE + 2, GRAY)
            continue
        i = idx - (1 if note else 0)
        if i < 0 or i >= len(lines):
            continue
        item = lines[i]
        text, is_code = (item, False) if isinstance(item, str) else item
        y = FIRST_BASE + row * font.yadv
        if is_code:
            if small is None:
                small = Font(os.path.join(ROOT, "include", "font_small.h"))
            fill(px, 0, y - 1, W, y + font.yadv, CODE_BG)
            fill(px, 0, y - 1, 3, y + font.yadv, CODE_EDGE)
            small.text(img, text, MARGIN + 6, y + 2, CODE_TEXT)
        else:
            font.text(img, text, MARGIN, y, color)

    # Счётчик «x/y» в верхнем левом углу полосы состояния.
    ind = f"{page + 1}/{total}"
    fill(img.load(), 0, 0, 6 * font.xadv, STATUS_H, BLACK)
    font.text(img, ind, MARGIN, PAGE_BASE - TEXT_H // 2, GRAY)
    return img


def screen_panel(font, url, ip, code, on=True, used=False, pct=78, rssi=4):
    """Экран веб-панели: выключатель. Включена — QR и состояние связи."""
    img = Image.new("RGB", (W, H), BLACK)
    status_bar(img, font, pct, rssi)
    if not on:
        font.text(img, "Панель выключена", 12, 40, RED)
        font.text(img, "кнопка A — включить", 12, 66, GRAY)
        font.text(img, "удержание B — назад", 12, 90, GRAY)
        return img

    import segno
    qs, qx, qy = 112, 4, 16
    qr = segno.make(url, error="l")
    qr.save("/tmp/_qr.png", scale=10, border=0, dark="#000000", light="#ffffff")
    qr_img = Image.open("/tmp/_qr.png").convert("RGB").resize((qs, qs), Image.NEAREST)
    img.paste(qr_img, (qx, qy))

    tx = qx + qs + 8
    font.text(img, "Веб-панель", tx, 18, GREEN)
    font.text(img, ip, tx, 38, WHITE)
    font.text(img, "код " + code, tx, 58, YELLOW)
    font.text(img, "на связи" if used else "ждём", tx, 78, GREEN if used else YELLOW)
    font.text(img, "A — выкл.", tx, 98, GRAY)
    return img



def screen_notice(font, text, pct=78, rssi=4):
    img = Image.new("RGB", (W, H), BLACK)
    status_bar(img, font, pct, rssi)
    # Сообщение бывает длиннее экрана: переносим, как в прошивке, и подгоняем
    # рамку под самую широкую строку.
    lines = wrap(font, text, W - 16)
    if len(lines) > 3:
        lines = lines[:3]
        lines[-1] += "…"
    if not lines:
        return img
    bw = max(font.width(s) for s in lines) + 12
    bh = len(lines) * font.yadv + 10
    bx = (W - bw) // 2
    by = (H - bh) // 2
    px = img.load()
    fill(px, bx, by, bx + bw, by + bh, PANEL)
    for x in range(bx, bx + bw):
        px[x, by] = px[x, by + bh - 1] = PANEL_EDGE
    for y in range(by, by + bh):
        px[bx, y] = px[bx + bw - 1, y] = PANEL_EDGE
    for i, s in enumerate(lines):
        font.text(img, s, bx + 6, by + 5 + i * font.yadv, WHITE)
    return img


# ---------------------------------------------------------------- текст
def wrap(font, text, max_px):
    lines, cur = [], ""
    for word in text.split(" "):
        cand = word if not cur else cur + " " + word
        if font.width(cand) <= max_px:
            cur = cand
        else:
            if cur:
                lines.append(cur)
            while font.width(word) > max_px:
                n = max_px // font.xadv
                lines.append(word[:n])
                word = word[n:]
            cur = word
    if cur:
        lines.append(cur)
    return lines


def screen_list(font, title, rows, index, pct=78, rssi=4):
    """Список: повторяет ui::list в src/ui.cpp.

    rows — список (текст, значение, метка). Значение "" — правого поля нет,
    метка: "installed" | "update" | None. Значение и метка делят одно место.
    """
    img = Image.new("RGB", (W, H), BLACK)
    px = img.load()
    status_bar(img, font, pct, rssi)

    font.text(img, title, MARGIN, 24, YELLOW)

    y = 44
    for i, (text, value, mark) in enumerate(rows):
        sel = (i == index)
        if sel:
            fill(px, 0, y, W, y + 20, SEL_BG)
            fill(px, 0, y, 3, y + 20, YELLOW)
        font.text(img, text, MARGIN + 6, y, WHITE if sel else GRAY)

        if value:
            vx = W - MARGIN - font.width(value)
            font.text(img, value, vx, y,
                      YELLOW if sel else (198, 198, 198))  # 0xC618
        else:
            cx, cy = W - 13, y + 8
            if mark == "installed":
                circle(px, cx, cy, 3, GREEN)
            elif mark == "update":
                ring(px, cx, cy, 6, ORANGE)
                circle(px, cx, cy, 3, ORANGE)
        y += 20
    return img


def screen_plugins(font, items, index, pct=78, rssi=4):
    return screen_list(font, "Плагины", [(n, "", m) for n, m in items], index,
                       pct, rssi)


def screen_settings(font, index, pct=78, rssi=4):
    return screen_list(font, "Настройки", [
        ("Плагины", "", None),
        ("Диалог и ответ", "", None),
        ("Питание", "", None),
        ("Кнопки", "", None),
        ("Сбросить настройки", "", None),
    ], index, pct, rssi)


def screen_power(font, index, pct=78, rssi=4):
    return screen_list(font, "Питание", [
        ("Экран гаснет", "20 с", None),
        ("Wi-Fi выкл.", "90 с", None),
        ("Лёгкий сон", "2 мин", None),
        ("Глубокий сон", "1 час", None),
    ], index, pct, rssi)


def screen_buttons(font, index, pct=78, rssi=4):
    return screen_list(font, "Кнопки", [
        ("Назад (удержание)", "0.85 с", None),
        ("Сброс сессии", "3 с", None),
    ], index, pct, rssi)


def screen_dialog(font, index, pct=78, rssi=4):
    return screen_list(font, "Диалог", [
        ("Длина ответа", "кратко", None),
        ("Память диалога", "авто", None),
        ("Лимит ответа", "авто 600", None),
    ], index, pct, rssi)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "images"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    font = Font(os.path.join(ROOT, "include", "font_jbmono.h"))
    portrait = load_image(os.path.join(ROOT, "include", "idle_image.h"))
    print(f"шрифт: {font.xadv}x{font.yadv}, картинка {portrait.size}")

    def save(img, name):
        p = os.path.join(args.out, name)
        img.resize((W * 3, H * 3), Image.NEAREST).save(p)
        print("  ", p)

    # --- PNG экранов
    save(screen_idle(font, portrait), "screen-idle.png")
    save(screen_recording(font, [int(140 + 100 * math.sin(i / 3.5)) % 256
                                 for i in range(WAVE_BARS)]), "screen-recording.png")
    save(screen_waiting(font, "Отвечаю"), "screen-waiting.png")
    answer = ("Go — компилируемый язык от Google: простой синтаксис, быстрая "
              "компиляция и хорошая работа с многопоточностью. Слабее он в GUI, "
              "ML и там, где нужны богатые абстракции.")
    lines = wrap(font, answer, W - 2 * MARGIN)
    # Пометка инструмента идёт первой строкой над ответом (как в прошивке).
    save(screen_answer(font, lines, 0, 2,
                       note="поиск: 5 источников, читал 3"), "screen-answer.png")
    save(screen_answer(font, [
        "Вот пример на Go:",
        ("func main() {", True),
        ('    fmt.Println("привет")', True),
        ("}", True),
        "Горутина запускается словом go.",
    ], 0, 1), "screen-code.png")
    save(screen_notice(font, "История разговора очищена"), "screen-notice.png")
    save(screen_panel(font, "http://192.168.1.42/?k=730512", "192.168.1.42",
                      "730512", on=True, used=True), "screen-panel.png")
    save(screen_panel(font, "", "", "", on=False), "screen-panel-off.png")
    save(screen_notice(font,
                       "не поставить: нет связи с raw.githubusercontent.com"),
         "screen-notice-long.png")
    save(screen_plugins(font, [("switch", "installed"), ("weather", "update"),
                               ("lamp", None), ("show_text", "installed")], 1),
         "screen-plugins.png")
    save(screen_settings(font, 2), "screen-settings.png")
    save(screen_dialog(font, 0), "screen-dialog.png")
    save(screen_power(font, 2), "screen-power.png")
    save(screen_buttons(font, 0), "screen-buttons.png")
    save(screen_tool(font, "ищу в интернете"), "screen-tool.png")

    # --- GIF: запись (волны + отсчёт)
    frames = []
    for k in range(24):
        waves = [max(0, int(120 + 110 * math.sin((i + k) / 3.5))) for i in range(WAVE_BARS)]
        frames.append(screen_recording(font, waves, 12000 - k * 250)
                      .resize((W * 2, H * 2), Image.NEAREST))
    frames[0].save(os.path.join(args.out, "demo-recording.gif"), save_all=True,
                   append_images=frames[1:], duration=90, loop=0, disposal=2)
    print("  ", os.path.join(args.out, "demo-recording.gif"))

    # --- GIF: точки
    frames = [screen_waiting(font, "Отвечаю").resize((W * 2, H * 2), Image.NEAREST)]
    for k in range(20):
        img = Image.new("RGB", (W, H), BLACK)
        status_bar(img, font, 78, 4)
        draw_dots(img, k * 45)
        frames.append(img.resize((W * 2, H * 2), Image.NEAREST))
    frames[0].save(os.path.join(args.out, "demo-waiting.gif"), save_all=True,
                   append_images=frames[1:], duration=60, loop=0, disposal=2)
    print("  ", os.path.join(args.out, "demo-waiting.gif"))

    # --- GIF: печатающийся текст
    frames = []
    words = answer.split(" ")
    for k in range(1, len(words) + 1):
        txt = " ".join(words[:k])
        frames.append(screen_answer(font, wrap(font, txt, W - 2 * MARGIN), 0, 1)
                      .resize((W * 2, H * 2), Image.NEAREST))
    frames[0].save(os.path.join(args.out, "demo-typing.gif"), save_all=True,
                   append_images=frames[1:], duration=140, loop=0, disposal=2)
    print("  ", os.path.join(args.out, "demo-typing.gif"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
