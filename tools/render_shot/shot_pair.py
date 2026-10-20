#!/usr/bin/env python3
"""Парный подбор серии снимков двух бэкендов (гейт паритета, docs/PLAN_NVRHI.md).

Зачем: один кадр двух прогонов сопоставить нельзя. Номер кадра — не время игры
(частота у DXVK и Vulkan различается в разы: замер 2026-10-20 на Wine-стенде
DXVK ~4 к/с против ~40 у Vulkan-бэкенда), а мир живёт по сетевым тикам, то есть
по часам. Поэтому снимки берутся серией по секундам от первого кадра
(`setvar auto_screenshot_time 60` + `setvar auto_screenshot_step 20` для любого
бэкенда; для Vulkan ещё `PW_VK_SHOT_TIME=60 PW_VK_SHOT_STEP=20` — снимок внутри
Present, это надёжный readback), а пара ищется по минимальному |Δ| среди всех
комбинаций.

Печатается: по каждому снимку — чёрные пиксели и светимость (отсекает чёрные
readback'и), матрица средних |Δ|, лучшая пара и диагональ (одинаковые секунды).
Дифференциал «одна и та же секунда» всё равно включает анимацию и камеру,
поэтому выводятся обе величины.

Использование:
  shot_pair.py 'ШАБЛОН_A.png' 'ШАБЛОН_B.png'
  напр.: shot_pair.py 'shots_dxvk/pw_auto_bb_t*.png' 'shots_vk/vk_shot_t*.bmp'
"""
import os
import sys
import glob

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_stats import load  # noqa: E402


def key(path):
    """Порядковый ключ снимка: t<секунды> из имени, иначе — первое число."""
    base = os.path.basename(path)
    i = base.find('t')
    if i >= 0 and base[i + 1:i + 2].isdigit():
        return int(''.join(c for c in base[i + 1:] if c.isdigit()))
    digits = ''.join(c for c in base if c.isdigit())
    return int(digits) if digits else 0


def brief(img, path):
    flat = img.reshape(-1, 3)
    lum = 0.299 * flat[:, 0] + 0.587 * flat[:, 1] + 0.114 * flat[:, 2]
    black = 100.0 * float(np.all(flat == 0, axis=1).mean())
    uniq = len(np.unique(flat, axis=0))
    print('  %-34s чёрных %6.2f%%  светимость %5.1f  цветов %d'
          % (os.path.basename(path), black, float(lum.mean()), uniq))
    return float(lum.mean()), black


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    a = sorted(glob.glob(sys.argv[1]), key=key)
    b = sorted(glob.glob(sys.argv[2]), key=key)
    if not a or not b:
        raise SystemExit('по шаблонам ничего не найдено: %r / %r' % (sys.argv[1], sys.argv[2]))
    print('A: %d снимков, B: %d снимков' % (len(a), len(b)))
    print('A:'); ia = []
    for p in a:
        im = load(p); brief(im, p); ia.append(im)
    print('B:'); ib = []
    for p in b:
        im = load(p); brief(im, p); ib.append(im)

    # матрица средних |Δ| (по всем каналам); размеры обязаны совпадать
    n, m = len(ia), len(ib)
    d = np.zeros((n, m))
    for i in range(n):
        for j in range(m):
            if ia[i].shape != ib[j].shape:
                d[i, j] = 999.0
                continue
            d[i, j] = float(np.abs(ia[i].astype(np.int16) - ib[j].astype(np.int16)).mean())
    print('\nматрица средних |Δ| (строки A, столбцы B):')
    print('        ' + ''.join('%8d' % key(b[j]) for j in range(m)))
    for i in range(n):
        print('%7d ' % key(a[i]) + ''.join('%8.1f' % d[i, j] for j in range(m)))
    bi, bj = np.unravel_index(int(np.argmin(d)), d.shape)
    print('\nлучшая пара: A %s  <->  B %s  |Δ| %.1f'
          % (os.path.basename(a[bi]), os.path.basename(b[bj]), d[bi, bj]))
    diag = [(i, j) for i in range(n) for j in range(m) if key(a[i]) == key(b[j])]
    if diag:
        i, j = diag[0]
        print('одинаковые секунды (%d): |Δ| %.1f' % (key(a[i]), d[i, j]))
    worst = float(d.max())
    print('максимум по матрице: %.1f (если лучшая пара близка к максимуму — '
          'кадры не совпадают, паритет мерить нечем)' % worst)


if __name__ == '__main__':
    main()
