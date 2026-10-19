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


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
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
