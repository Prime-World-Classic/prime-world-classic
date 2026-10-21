#!/usr/bin/env python3
"""A/B по регионам кадра: метрики не зависят от зума камеры и фазы матча.

Считает по заданным прямоугольникам для двух серий снимков (DXVK vs Vulkan):
среднюю светимость, долю чёрных, долю белых, число уникальных цветов и
среднюю |Δ| между средними цветами региона (по парам одинаковых секунд).

Использование: region_ab.py 'ШАБЛОН_A' 'ШАБЛОН_B'
"""
import sys, glob
import numpy as np
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_stats import load as _load

REGIONS = [
    ("minimap",      810, 555, 1021, 739),
    ("ability_bar",  200, 695,  800, 739),
    ("plate_area",    29, 443,  360, 575),
    ("hero_portrait",  0, 610,  180, 739),
    ("right_units",  780, 250,  980, 500),
    ("center_world", 300, 200,  700, 450),
    ("hud_top",      350,  80,  650, 200),
]


def load(p):
    return _load(p).astype(np.int16)


def stats(a, x0, y0, x1, y1):
    r = a[y0:y1, x0:x1].reshape(-1, 3)
    lum = (0.299 * r[:, 0] + 0.587 * r[:, 1] + 0.114 * r[:, 2])
    black = float(np.mean(lum < 6)) * 100
    white = float(np.mean(np.all(r >= 250, axis=1))) * 100
    uniq = len(set(map(tuple, r[::7])))
    return r.mean(axis=0), lum.mean(), black, white, uniq


def series(pat):
    out = {}
    for p in sorted(glob.glob(pat), key=lambda s: int(s.split("_t")[-1].split(".")[0])):
        out[int(p.split("_t")[-1].split(".")[0])] = load(p)
    return out


def main():
    A, B = series(sys.argv[1]), series(sys.argv[2])
    common = sorted(set(A) & set(B))
    if not common:
        sys.exit("нет общих снимков: шаблоны %r / %r не пересеклись" % (sys.argv[1], sys.argv[2]))
    print("секунды: A=%s B=%s общие=%s" % (sorted(A), sorted(B), common))
    hdr = "%-14s %9s %9s %9s %9s %9s %9s" % ("region", "meanD_A", "meanD_B",
                                             "lumA", "lumB", "blackA%", "blackB%")
    print(hdr)
    for name, x0, y0, x1, y1 in REGIONS:
        ma = np.mean([stats(A[t], x0, y0, x1, y1)[0] for t in common], axis=0)
        mb = np.mean([stats(B[t], x0, y0, x1, y1)[0] for t in common], axis=0)
        la = np.mean([stats(A[t], x0, y0, x1, y1)[1] for t in common])
        lb = np.mean([stats(B[t], x0, y0, x1, y1)[1] for t in common])
        ba = np.mean([stats(A[t], x0, y0, x1, y1)[2] for t in common])
        bb = np.mean([stats(B[t], x0, y0, x1, y1)[2] for t in common])
        d = float(np.mean(np.abs(ma - mb)))
        print("%-14s %9.1f %9.1f %9.1f %9.1f %9.2f %9.2f  |D|=%.1f uniqA=%d uniqB=%d wA=%.2f wB=%.2f"
              % (name, ma.mean(), mb.mean(), la, lb, ba, bb, d,
                 np.mean([stats(A[t], x0, y0, x1, y1)[4] for t in common]),
                 np.mean([stats(B[t], x0, y0, x1, y1)[4] for t in common]),
                 np.mean([stats(A[t], x0, y0, x1, y1)[3] for t in common]),
                 np.mean([stats(B[t], x0, y0, x1, y1)[3] for t in common])))


main()
