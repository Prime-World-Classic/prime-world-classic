#!/usr/bin/env python3
"""Ищет масштаб+сдвиг, при котором кадр B ложится на кадр A.

Зачем: регионный A/B даёт «средняя совпала, а пиксели разные» (gross 20…45 %
при |D| по средней 1…4). Так выглядит и разный ракурс, и другой внутренний
размер рендера (backbuffer отмасштабирован в окно), и другой фильтр
сэмплирования. Первые два проверяются геометрией: если кадр B сходится с A при
s != 1 — это масштаб, а не содержание.

Использование: scale_probe.py A.png B.png [x0 y0 x1 y1]
  Прямоугольник по умолчанию — hero_portrait из region_ab.py (HUD: он не
  зависит от камеры, поэтому масштаб ищем именно по нему).

Метрика: gross = доля пикселей региона с |Δ|>32 хотя бы по одному каналу
(та же, что в region_ab.py), плюс mean|Δ|. Поиск в два шага: сначала сдвиг при
s=1, потом s по 0,90…1,10 с сдвигом ±4 вокруг найденного.
"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_stats import load as _load
from region_ab import REGIONS

STEP = 12


def gross(a, b):
    d = np.abs(a - b).max(axis=2)
    return float(np.mean(d > 32)) * 100.0, float(np.abs(a - b).mean())


def crop(img, x0, y0, w, h):
    return img[y0:y0 + h, x0:x0 + w]


def best_shift(A, B, x0, y0, w, h, rng):
    """B уже отмасштабирован; ищем целочисленный сдвиг окна на B.

    Отрицательные координаты отбрасываются: срез с отрицательным началом
    в numpy берётся с конца массива, и метрика врёт молча.
    """
    best = (1e9, 0, 0)
    ca = crop(A, x0, y0, w, h)
    H, W = B.shape[:2]
    for dy in range(-rng, rng + 1):
        for dx in range(-rng, rng + 1):
            bx, by = x0 + dx, y0 + dy
            if bx < 0 or by < 0 or bx + w > W or by + h > H:
                continue
            v = gross(ca, crop(B, bx, by, w, h))[0]
            if v < best[0]:
                best = (v, dx, dy)
    return best


def shifted(a, dy, dx):
    """Сдвиг с продолжением края (граница не должна давать артефакт в метрике)."""
    H, W = a.shape[:2]
    out = np.empty_like(a)
    ys, yd = (slice(dy, H), slice(0, H - dy)) if dy >= 0 else (slice(0, H + dy), slice(-dy, H))
    xs, xd = (slice(dx, W), slice(0, W - dx)) if dx >= 0 else (slice(0, W + dx), slice(-dx, W))
    out[yd, xd] = a[ys, xs]
    return out


def blur(a, r):
    acc = np.zeros(a.shape, dtype=np.float32)
    n = 0
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            acc += shifted(a, dy, dx)
            n += 1
    return acc / n


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    A = _load(sys.argv[1]).astype(np.int16)
    B0 = _load(sys.argv[2])
    if len(sys.argv) > 6:
        x0, y0, x1, y1 = [int(v) for v in sys.argv[3:7]]
    else:
        _, x0, y0, x1, y1 = REGIONS[3]
    w, h = x1 - x0, y1 - y0
    H, W = B0.shape[:2]

    v0, dx0, dy0 = best_shift(A, B0, x0, y0, w, h, STEP)
    ca = crop(A, x0, y0, w, h)
    cb = crop(B0.astype(np.int16), x0 + dx0, y0 + dy0, w, h)
    print("сдвиг при s=1: (%+d,%+d)" % (dx0, dy0))
    print("размытие (gross%% и mean|Δ| при этом сдвиге):")
    for r in (0, 1, 2):
        print("  A размыто r=%d: gross %5.1f%%  mean|Δ| %6.2f"
              % (r, gross(blur(ca, r).astype(np.int16), cb)[0],
                 gross(blur(ca, r).astype(np.int16), cb)[1]))
        print("  B размыто r=%d: gross %5.1f%%  mean|Δ| %6.2f"
              % (r, gross(ca, blur(cb, r).astype(np.int16))[0],
                 gross(ca, blur(cb, r).astype(np.int16))[1]))

    def nn(img, s):
        """NEAREST-масштаб без PIL (в окружении стенда PIL нет, см. shot_stats.load)."""
        H, W = img.shape[:2]
        bw, bh = max(1, int(round(W * s))), max(1, int(round(H * s)))
        ci = (np.arange(bw) * W) // bw
        ri = (np.arange(bh) * H) // bh
        return img[ri][:, ci]

    res = []
    s = 0.90
    while s <= 1.1001:
        B = nn(B0, s).astype(np.int16)
        sx, sy = int(round(x0 * s)) - x0, int(round(y0 * s)) - y0
        v, dx, dy = best_shift(A, B, x0, y0, w, h, 4)
        res.append((v, s, dx + sx, dy + sy))
        s += 0.01
    res.sort()
    bad = [r for r in res if r[0] >= 1e8]
    res = [r for r in res if r[0] < 1e8]
    print("лучшие масштабы (gross, s, сдвиг):")
    for v, s, dx, dy in res[:8]:
        print("  gross %5.1f%%  s=%.3f  сдвиг (%+d,%+d)" % (v, s, dx, dy))
    if res:
        print("худший допустимый: gross %.1f%% (s=%.3f)" % (res[-1][0], res[-1][1]))
    if bad:
        print("  %d масштабов не имели ни одной допустимой позиции окна"
              % len(bad))


main()
