#!/usr/bin/env python3
"""Скриншот-гейт рендер-этапов (план NVRHI): сравнение кадров + PPM->PNG.

Кадр снимает существующий инструмент:
    python3 ../../../vm7/vncshot.py 127.0.0.1 5918 /tmp/shot.ppm

Затем:
    python3 ppm_tools.py diff /tmp/shot.ppm /tmp/ref.ppm [--tol 8] [--limit 0.0]
    python3 ppm_tools.py png /tmp/shot.ppm /tmp/shot.png     # чтобы посмотреть глазами

PPM (P6) трогать проще всего: заголовок ASCII, дальше RGB-байты строками.
Код выхода ненулевой, если доля отличающихся пикселей выше --limit (по
умолчанию 0 — картинка обязана совпадать; для этапов с осознанным визуальным
дрейфом порог выставляют явно).
"""
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise RuntimeError("%s: ожидается P6 PPM" % path)
    pos = 2
    vals = []
    while len(vals) < 3:
        # проускаем пробелы и комментарии
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while data[end:end + 1] not in b" \n\t":
            end += 1
        vals.append(int(data[pos:end]))
        pos = end
    w, h, maxv = vals
    if maxv != 255:
        raise RuntimeError("%s: maxval=%d, ожидается 255" % (path, maxv))
    hdr = pos + 1
    body = data[hdr:]
    if len(body) < w * h * 3:
        raise RuntimeError("%s: данных меньше, чем %d байт" % (path, w * h * 3))
    return w, h, body


def diff(a, b, tol=8, limit=0.0):
    w1, h1, a_body = read_ppm(a)
    w2, h2, b_body = read_ppm(b)
    if (w1, h1) != (w2, h2):
        print("размеры разные: %dx%d vs %dx%d" % (w1, h1, w2, h2))
        return 2
    n = w1 * h1
    bad = 0
    maxd = 0
    first = None
    for i in range(0, n * 3, 3):
        d = max(abs(a_body[i] - b_body[i]), abs(a_body[i + 1] - b_body[i + 1]), abs(a_body[i + 2] - b_body[i + 2]))
        if d > maxd:
            maxd = d
        if d > tol:
            bad += 1
            if first is None:
                px = i // 3
                first = (px % w1, px // w1)
    ratio = 100.0 * bad / n
    print("кадр %dx%d; отличающихся пикселей (>%d на канал): %d (%.4f%%); max|d|=%d%s"
          % (w1, h1, tol, bad, ratio, maxd, "; первый в %s" % (first,) if first else ""))
    return 1 if ratio > limit else 0


def to_png(src, dst):
    w, h, body = read_ppm(src)
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)                                   # filter: None
        row = body[y * stride:(y + 1) * stride]
        for x in range(w):                              # RGB -> RGBA
            raw += row[x * 3:x * 3 + 3] + b"\xff"

    def chunk(tag, data):
        return (struct_pack(len(data)) + tag + data
                + struct_pack(zlib.crc32(tag + data) & 0xFFFFFFFF))

    def struct_pack(v):
        import struct
        return struct.pack("!I", v)

    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", ihdr(w, h))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
                + chunk(b"IEND", b""))
    print("%s -> %s (%dx%d)" % (src, dst, w, h))
    return 0


def ihdr(w, h):
    import struct
    return struct.pack("!IIBBBBB", w, h, 8, 6, 0, 0, 0)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "diff":
        tol, limit = 8, 0.0
        rest = [sys.argv[2]]
        args = sys.argv[3:]
        i = 0
        while i < len(args):
            if args[i] == "--tol":
                tol = int(args[i + 1]); i += 2
            elif args[i] == "--limit":
                limit = float(args[i + 1]); i += 2
            else:
                rest.append(args[i]); i += 1
        if len(rest) < 2:
            print("diff: нужны два PPM")
            return 2
        return diff(rest[0], rest[1], tol, limit)
    if cmd == "png":
        return to_png(sys.argv[2], sys.argv[3])
    print("неизвестная команда: %s" % cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main())
