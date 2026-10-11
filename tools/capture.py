#!/usr/bin/env python3
"""Снимает запись с M5StickC PLUS2 и складывает в WAV.

Использование:
    python3 tools/capture.py test.wav
    python3 tools/capture.py test.wav --port /dev/cu.usbserial-XXXX
    python3 tools/capture.py test.wav --stdin      # для тестов без железа

Скрипт сам открывает serial-порт и дёргает DTR/RTS, чтобы перезагрузить
плату: `pio device monitor` для этого не годится — он отказывается
работать с перенаправленным stdin.

Протокол (см. src/probe_main.cpp):
    PROBE META <dataBytes> <sampleRate>
    <строки base64>
    PROBE END
"""
import argparse
import base64
import struct
import sys
import time


DEFAULT_PORT = "/dev/cu.usbserial-5A6D0057111"


def write_wav(path, pcm, sample_rate):
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate,
                                    sample_rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    with open(path, "wb") as f:
        f.write(header)
        f.write(pcm)


def reset_board(ser):
    """Импульс на DTR/RTS перезагружает ESP32 в прошивку."""
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.15)
    ser.setRTS(False)
    time.sleep(0.05)


def lines_from_serial(port, baud):
    import serial  # pyserial; импортируем здесь, чтобы --stdin не требовал его

    ser = serial.Serial(port, baud, timeout=1)
    try:
        reset_board(ser)
        while True:
            try:
                raw = ser.readline()
            except Exception as exc:
                print(f"[capture] ошибка чтения: {exc}", file=sys.stderr)
                return
            if not raw:
                continue
            yield raw.decode("utf-8", errors="replace")
    finally:
        ser.close()


def collect(lines, out_path):
    data_bytes = None
    sample_rate = 16000
    chunks = []
    seen_end = False

    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("PROBE META"):
            parts = line.split()
            if len(parts) >= 4:
                data_bytes = int(parts[2])
                sample_rate = int(parts[3])
                print(f"[capture] ожидаю {data_bytes} байт, "
                      f"{sample_rate} Гц", file=sys.stderr, flush=True)
            continue
        if line == "PROBE END":
            seen_end = True
            break
        # До META идёт загрузочный лог ESP32 и M5GFX — его нельзя
        # подмешивать в base64, иначе декодер упадёт.
        if data_bytes is None:
            print(line, file=sys.stderr, flush=True)
            continue
        if line.startswith("[probe]") or line.startswith("PROBE"):
            print(line, file=sys.stderr, flush=True)
            continue
        chunks.append(line)

    if data_bytes is None:
        print("[capture] ОШИБКА: не дождался строки PROBE META. "
              "Устройство не перезагрузилось или прошивка не та.",
              file=sys.stderr)
        return 1
    if not seen_end:
        print("[capture] предупреждение: PROBE END не пришёл, "
              "собираю что есть", file=sys.stderr)

    b64_text = "".join(chunks)
    try:
        pcm = base64.b64decode(b64_text, validate=True)
    except Exception as exc:
        print(f"[capture] ОШИБКА: base64 не разобрался: {exc}",
              file=sys.stderr)
        return 1

    if len(pcm) != data_bytes:
        print(f"[capture] ОШИБКА: получил {len(pcm)} байт, "
              f"ждал {data_bytes}. Похоже на обрыв.", file=sys.stderr)
        return 1

    write_wav(out_path, pcm, sample_rate)
    duration = len(pcm) / (sample_rate * 2)
    print(f"[capture] Готово: {out_path} ({len(pcm)} байт, "
          f"{duration:.1f} с)", file=sys.stderr)
    print(f"[capture] Послушать: afplay {out_path}", file=sys.stderr)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", nargs="?", default="test.wav",
                    help="куда положить WAV (по умолчанию test.wav)")
    ap.add_argument("--port", default=DEFAULT_PORT,
                    help=f"serial-порт (по умолчанию {DEFAULT_PORT})")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--stdin", action="store_true",
                    help="читать поток со stdin вместо порта (для тестов)")
    args = ap.parse_args()

    if args.stdin:
        return collect(sys.stdin, args.out)
    try:
        return collect(lines_from_serial(args.port, args.baud), args.out)
    except ImportError:
        print("[capture] ОШИБКА: нет pyserial. Поставь: pip install pyserial",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
