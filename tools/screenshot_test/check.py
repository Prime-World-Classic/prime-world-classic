#!/usr/bin/env python3
"""check.py — сверка файлов, написанных Render/screenshot.cpp, с узором из main.cpp.

Декодирует png/bmp/dds штатными средствами (zlib из stdlib) и сравнивает
пиксели. Логический пиксель узора: R=c2, G=c1, B=c0, A=c3, где
  c0=(x*7+y)&255, c1=(y*19+3)&255, c2=(x+y*5+11)&255, c3=(x*5+y*2)&255.
Переворот строк, перестановка каналов и ошибка шага строки дают здесь разные
расхождения — то есть тест различает конкретную поломку, а не «что-то не так».
"""
import struct
import sys
import zlib

W, H = 37, 13


def want(x, y):
    c0 = (x * 7 + y) & 0xFF
    c1 = (y * 19 + 3) & 0xFF
    c2 = (x + y * 5 + 11) & 0xFF
    c3 = (x * 5 + y * 2) & 0xFF
    return (c2, c1, c0, c3)  # R,G,B,A


def diff_pixels(got, alpha_ignored=False):
    """got[(x,y)] = (r,g,b,a) сверху вниз. Возвращает список расхождений."""
    bad = []
    for y in range(H):
        for x in range(W):
            g = got[(x, y)]
            w = want(x, y)
            if g[:3] != w[:3] or (not alpha_ignored and g[3] != w[3]):
                bad.append(((x, y), g, w))
                if len(bad) >= 5:
                    break
        if len(bad) >= 5:
            break
    return bad


def read_png(path, want_alpha):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("нет сигнатуры PNG")
    i = 8
    idat = bytearray()
    ihdr = None
    seen = []
    while i < len(d):
        (ln,) = struct.unpack_from(">I", d, i)
        typ = d[i + 4:i + 8]
        body = d[i + 8:i + 8 + ln]
        (crc,) = struct.unpack_from(">I", d, i + 8 + ln)
        if crc != zlib.crc32(typ + body) & 0xFFFFFFFF:
            raise AssertionError("битый CRC у чанка %s" % typ)
        seen.append(typ.decode())
        if typ == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        i += 12 + ln
    w, h, depth, ctype, comp, filt, inter = ihdr
    if (w, h) != (W, H) or depth != 8 or inter != 0:
        raise AssertionError("IHDR: %r" % (ihdr,))
    ch = 4 if ctype == 6 else 3
    if ctype not in (2, 6) or (ch == 4) != want_alpha:
        raise AssertionError("цветовой тип %d, ожидался %d" % (ctype, 6 if want_alpha else 2))
    raw = zlib.decompress(bytes(idat))
    stride = W * ch
    if len(raw) != H * (stride + 1):
        raise AssertionError("размер IDAT-потока %d, ожидалось %d" % (len(raw), H * (stride + 1)))
    prev = bytearray(stride)
    out = {}
    pos = 0
    for y in range(H):
        f = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if f == 0:
            pass
        elif f == 1:      # Sub
            for k in range(ch, stride):
                line[k] = (line[k] + line[k - ch]) & 0xFF
        elif f == 2:      # Up
            for k in range(stride):
                line[k] = (line[k] + prev[k]) & 0xFF
        elif f == 3:      # Average
            for k in range(stride):
                a = line[k - ch] if k >= ch else 0
                line[k] = (line[k] + ((a + prev[k]) >> 1)) & 0xFF
        elif f == 4:      # Paeth
            for k in range(stride):
                a = line[k - ch] if k >= ch else 0
                b = prev[k]
                c = prev[k - ch] if k >= ch else 0
                p, pa, pb, pc = b + c - a, abs(b + c - 2 * a), abs(b - a), abs(c - a)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[k] = (line[k] + pr) & 0xFF
        else:
            raise AssertionError("неизвестный фильтр %d" % f)
        for x in range(W):
            px = line[x * ch:x * ch + ch]
            out[(x, y)] = (px[0], px[1], px[2], px[3] if ch == 4 else 255)
        prev = line
    return out, seen


def read_bmp(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise AssertionError("нет сигнатуры BMP")
    off, = struct.unpack_from("<I", d, 10)
    hsz, = struct.unpack_from("<I", d, 14)
    if hsz != 40:
        raise AssertionError("DIB не 40 байт: %d" % hsz)
    # w/h/planes/bpp/comp читаются ниже раздельно (тут нужны signed h и HH
    # для planes/bpp — одним форматом их не снять)
    w, h = struct.unpack_from("<ii", d, 18)
    # biPlanes @26, biBitCount @28, biCompression @30 (DIB начинается с 14)
    planes, bpp = struct.unpack_from("<HH", d, 26)
    comp, = struct.unpack_from("<I", d, 30)
    if (w, h) != (W, H) or bpp != 32 or comp != 0 or planes != 1:
        raise AssertionError("BMP заголовок: w=%d h=%d bpp=%d comp=%d" % (w, h, bpp, comp))
    out = {}
    for y in range(H):
        # строки снизу вверх
        row = off + (H - 1 - y) * W * 4
        for x in range(W):
            b, g, r, a = d[row + x * 4:row + x * 4 + 4]
            out[(x, y)] = (r, g, b, a)
    return out


def read_dds(path):
    d = open(path, "rb").read()
    if d[:4] != b"DDS ":
        raise AssertionError("нет сигнатуры DDS")
    hsz, = struct.unpack_from("<I", d, 4)
    if hsz != 124:
        raise AssertionError("dwSize = %d, ожидалось 124" % hsz)
    flags, hh, ww, pitch = struct.unpack_from("<IIII", d, 8)
    if (ww, hh) != (W, H) or pitch != W * 4:
        raise AssertionError("DDS: w=%d h=%d pitch=%d" % (ww, hh, pitch))
    pf_size, pf_flags = struct.unpack_from("<II", d, 76)
    bitcount, = struct.unpack_from("<I", d, 88)
    # ddspf: dwSize@76, dwFlags@80, dwFourCC@84, dwRGBBitCount@88, маски @92..104
    rm, gm, bm, am = struct.unpack_from("<IIII", d, 92)
    if pf_size != 32 or bitcount != 32 or (rm, gm, bm, am) != (0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000):
        raise AssertionError("DDPIXELFORMAT: size=%d flags=%d bits=%d masks=%08x/%08x/%08x/%08x"
                             % (pf_size, pf_flags, bitcount, rm, gm, bm, am))
    out = {}
    off = 128
    for y in range(H):
        for x in range(W):
            b, g, r, a = d[off + (y * W + x) * 4: off + (y * W + x) * 4 + 4]
            out[(x, y)] = (r, g, b, a)
    return out


def check(name, got, alpha_ignored=False):
    bad = diff_pixels(got, alpha_ignored)
    if bad:
        for pos, g, w in bad[:3]:
            print("  FAIL %s (%d,%d): получено %r, ожидалось %r" % (name, pos[0], pos[1], g, w))
        return False
    print("  OK   %s" % name)
    return True


def pil_crosscheck():
    """Независимая сверка тем же файлом, что читает любой декодер (PIL).
    Нужна ровно потому, что собственный декодер выше может делить с писателем
    одну и ту же ошибочную догадку (так и вышло с DDS: писатель и декодер
    по-разному, но оба неверно считали смещения заголовка).
    PIL не отдаёт альфу у 32-битного BMP и DDS — там сверяется только RGB."""
    try:
        from PIL import Image
    except ImportError:
        print("  SKIP PIL-сверка (PIL не установлен)")
        return True
    ok = True
    for name, check_alpha in (("out.png", True), ("out_rgb.png", False),
                              ("out.bmp", False), ("out.dds", False)):
        im = Image.open(name)
        if im.size != (W, H):
            print("  FAIL PIL %s: размер %r" % (name, im.size))
            ok = False
            continue
        px = im.convert("RGBA").load()
        bad = [((x, y), px[x, y], want(x, y)) for y in range(H) for x in range(W)
               if px[x, y][:3] != want(x, y)[:3] or (check_alpha and px[x, y][3] != want(x, y)[3])]
        if bad:
            print("  FAIL PIL %s: %r != %r" % (name, bad[0][1], bad[0][2]))
            ok = False
        else:
            print("  OK   PIL %s (%s)" % (name, im.mode))
    return ok


def main():
    ok = True

    png, seen = read_png("out.png", True)
    if seen != ["IHDR", "IDAT", "IEND"]:
        print("  FAIL out.png: набор чанков %r" % seen)
        ok = False
    ok &= check("out.png (BGRA-источник, с альфой)", png)

    png2, _ = read_png("out_rgb.png", False)
    ok &= check("out_rgb.png (альфа отброшена)", png2, alpha_ignored=True)

    png3, _ = read_png("out_rgba.png", True)
    ok &= check("out_rgba.png (RGBA-источник — должен совпасть с out.png)", png3)
    if png3 != png:
        print("  FAIL: RGBA- и BGRA-источники дали разную картинку")
        ok = False

    bmp = read_bmp("out.bmp")
    ok &= check("out.bmp (альфа в BMP не используется)", bmp, alpha_ignored=True)

    dds = read_dds("out.dds")
    ok &= check("out.dds (BGRA-источник)", dds)

    dds2 = read_dds("out_rgba.dds")
    ok &= check("out_rgba.dds (RGBA-источник)", dds2)
    if dds2 != dds:
        print("  FAIL: DDS по RGBA- и BGRA-источникам различаются")
        ok = False

    dds3 = read_dds("out_mem.dds")
    ok &= check("out_mem.dds (BuildDdsImage)", dds3)

    ok &= pil_crosscheck()

    print("итог: %s" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
