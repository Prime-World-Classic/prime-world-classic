#!/usr/bin/env python3
"""Метрики снимка кадра (BMP/PNG) и сравнение с эталоном.

Зачем: глазком «чёрный/не чёрный» различать нельзя — в прогонах Vulkan-бэкенда
кадр выглядел «сценой», но оказывался 99% чёрным (насыщение NaN/огромных
значений в UNORM даёт ровно 0). Скрипт даёт числа: сколько пикселей чёрных,
какая средняя светимость, сколько уникальных цветов, и (при эталоне) насколько
кадр близок к DXVK-картинке.

Эталон: tools/render_shot/refs/wine-dxvk-2026-10-15/pw_auto_bb_3000.png
(полный кадр 1024x768, DXVK-путь того же клиента).

Использование:
  shot_stats.py КАДР.bmp [ЭТАЛОН.png]
  shot_stats.py --defects КАДР.bmp [КАДР2.bmp ...]
     метрики дефектов, НЕ требующие сопоставленного кадра: сколько пикселей
     чисто белых / пурпурных (признаки подстановки несвязанной текстуры) и
     площадь крупнейшего белого прямоугольника. Сравнение «кадр vs эталон"
     на разных моментах сцены бессмысленно (см. PLAN_NVRHI, раздел про гейт
     паритета), а эти числа сравниваются между прогонами.
"""
import subprocess
import sys
import tempfile

import numpy as np


def load(path):
    """Любой BMP/PNG -> (H, W, 3) uint8 в порядке RGB."""
    if path.lower().endswith('.bmp'):
        d = open(path, 'rb').read()
        off = int.from_bytes(d[10:14], 'little')
        w = int.from_bytes(d[18:22], 'little')
        h = int.from_bytes(d[22:26], 'little')
        if h > 0x7FFFFFFF:
            h -= 1 << 32          # отрицательная высота = top-down
        bpp = int.from_bytes(d[28:30], 'little')
        if bpp != 24:
            raise SystemExit('ожидается 24-битный BMP, получен bpp=%d' % bpp)
        row = ((w * 3 + 3) // 4) * 4
        a = np.frombuffer(d, dtype=np.uint8)
        img = np.empty((abs(h), w, 3), dtype=np.uint8)
        for y in range(abs(h)):
            base = off + y * row
            img[abs(h) - 1 - y if h > 0 else y] = a[base:base + w * 3].reshape(w, 3)[:, ::-1]
        return img
    with tempfile.NamedTemporaryFile(suffix='.rgb', delete=False) as f:
        tmp = f.name
    subprocess.run(['convert', path, '-depth', '8', 'rgb:' + tmp], check=True)
    raw = open(tmp, 'rb').read()
    size = subprocess.run(['identify', '-format', '%w %h', path],
                          capture_output=True, text=True).stdout.split()
    w, h = int(size[0]), int(size[1])
    return np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3)


def stats(img, tag):
    h, w, _ = img.shape
    flat = img.reshape(-1, 3)
    lum = (0.299 * flat[:, 0] + 0.587 * flat[:, 1] + 0.114 * flat[:, 2])
    black = int(np.all(flat == 0, axis=1).sum())
    uniq = len(np.unique(flat, axis=0))
    print('%s: %dx%d  чёрных %d (%.2f%%)  средних %d  уникальных цветов %d'
          % (tag, w, h, black, 100.0 * black / flat.shape[0],
             int((lum > 8).sum()), uniq))
    # доминирующие цвета
    view = np.ascontiguousarray(flat).view(np.dtype((np.void, flat.dtype.itemsize * 3)))
    uq, cnt = np.unique(view, return_counts=True)
    colors = np.frombuffer(uq.tobytes(), dtype=np.uint8).reshape(-1, 3)
    for i in np.argsort(-cnt)[:6]:
        c = colors[i]
        print('    RGB(%3d,%3d,%3d) x %d' % (c[0], c[1], c[2], cnt[i]))
    return lum


def largest_rect(mask):
    """Площадь крупнейшего сплошного прямоугольника в булевой маске (H,W)
    и его координаты. Классика «largest rectangle in histogram»."""
    h, w = mask.shape
    heights = np.zeros(w, dtype=np.int32)
    best = (0, 0, 0, 0, 0)          # площадь, x0, y0, x1, y1
    for y in range(h):
        row = mask[y]
        heights = np.where(row, heights + 1, 0)
        stack = []                  # (столбец, высота)
        for x in range(w + 1):
            hh = int(heights[x]) if x < w else 0
            start = x
            while stack and stack[-1][1] >= hh:
                sx, sh = stack.pop()
                area = sh * (x - sx)
                if area > best[0]:
                    best = (area, sx, y - sh + 1, x, y + 1)
                start = sx
            stack.append((start, hh))
    return best


def defects(img, tag):
    h, w, _ = img.shape
    flat = img.reshape(-1, 3).astype(np.int16)
    white = np.all(flat >= 250, axis=1).reshape(h, w)
    mag = ((flat[:, 0] >= 230) & (flat[:, 1] <= 30) & (flat[:, 2] >= 230)).reshape(h, w)
    blk = np.all(flat == 0, axis=1).reshape(h, w)
    print('%s: %dx%d  белых %d (%.2f%%)  пурпурных %d (%.2f%%)  чёрных %d (%.2f%%)'
          % (tag, w, h, int(white.sum()), 100.0 * white.mean(),
             int(mag.sum()), 100.0 * mag.mean(), int(blk.sum()), 100.0 * blk.mean()))
    for name, m in (('белый', white), ('пурпурный', mag)):
        area, x0, y0, x1, y1 = largest_rect(m)
        if area:
            print('    крупнейший прямоугольник %s: %dx%d = %d px в (%d,%d)-(%d,%d)'
                  % (name, x1 - x0, y1 - y0, area, x0, y0, x1, y1))
    return white, mag


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == '--defects':
        for p in sys.argv[2:]:
            defects(load(p), p)
        return
    img = load(sys.argv[1])
    lum = stats(img, 'КАДР ' + sys.argv[1])
    if len(sys.argv) > 2:
        ref = load(sys.argv[2])
        if ref.shape != img.shape:
            print('эталон %dx%d, кадр %dx%d — сравнение по центру пересечения'
                  % (ref.shape[1], ref.shape[0], img.shape[1], img.shape[0]))
            h = min(ref.shape[0], img.shape[0]); w = min(ref.shape[1], img.shape[1])
            ref, img = ref[:h, :w], img[:h, :w]
        rlum = stats(ref, 'ЭТАЛОН ' + sys.argv[2])
        d = np.abs(img.astype(np.int16) - ref.astype(np.int16)).max(axis=2)
        print('расхождение: средняя |Δ| %.1f, пикселей с |Δ|>32: %d (%.2f%%)'
              % (d.mean(), int((d > 32).sum()), 100.0 * (d > 32).mean()))
        print('светимость: кадр %.1f, эталон %.1f' % (lum.mean(), rlum.mean()))


if __name__ == '__main__':
    main()
