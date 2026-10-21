#!/usr/bin/env python3
"""A/B по регионам кадра: метрики не зависят от зума камеры и фазы матча.

Считает по заданным прямоугольникам для двух серий снимков (DXVK vs Vulkan):
среднюю светимость, долю чёрных, долю белых, число уникальных цветов, среднее
B-R и тон-расхождение (L1 гистограмм тона, см. shot_stats.py --tone) между
средними цветами региона (по парам одинаковых секунд).

Использование: region_ab.py 'ШАБЛОН_A' 'ШАБЛОН_B' [ШАБЛОН_ШУМ]
  Третий шаблон — повторный прогон ТОЙ ЖЕ стороны, что и A (иначе «шум»
  считается между разными бэкендами и полосу шума не даёт). Без него |D|
  региона неотличим от разницы двух разных матчей (порог гейта: сигнал,
  если |D| > 2 x шум).
"""
import sys, glob
import numpy as np
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_stats import load as _load, tone_hist as _tone, TONE_BINS

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
    crop = a[y0:y1, x0:x1]
    r = crop.reshape(-1, 3)
    lum = (0.299 * r[:, 0] + 0.587 * r[:, 1] + 0.114 * r[:, 2])
    black = float(np.mean(lum < 6)) * 100
    white = float(np.mean(np.all(r >= 250, axis=1))) * 100
    uniq = len(set(map(tuple, r[::7])))
    br = float((r[:, 2] - r[:, 0]).mean())
    # детальность = средняя величина соседних перепадов (энергия граней).
    # Нужна, потому что «однотонный пол» и «пол со швами» по средней
    # светимости неразличимы (замер 2026-10-21: right_units |D| по средней 4,1)
    c = crop.astype(np.float32)
    det = float(np.abs(np.diff(c, axis=1)).mean() + np.abs(np.diff(c, axis=0)).mean())
    return r.mean(axis=0), lum.mean(), black, white, uniq, br, _tone(np.ascontiguousarray(crop)), det


def series(pat):
    out = {}
    for p in sorted(glob.glob(pat), key=lambda s: int(s.split("_t")[-1].split(".")[0])):
        a = load(p)
        # чёрный readback (кадр до первого рисующего Present — у Vulkan-бэкенда
        # t0 такой всегда) попадая в среднее по парам враньём портит каждую
        # метрику региона: выбрасываем кадр целиком, а не регион
        if float(np.mean(np.all(a == 0, axis=2))) > 0.5:
            print("пропущен чёрный кадр: %s" % p)
            continue
        out[int(p.split("_t")[-1].split(".")[0])] = a.astype(np.int16)
    return out


def pair_stats(A, B, common, x0, y0, x1, y1):
    for t in common:
        if A[t].shape != B[t].shape:
            sys.exit("размеры кадра разные: t=%s %r vs %r — регионные метрики "
                     "имеют смысл только при одном окне на обеих сторонах"
                     % (t, A[t].shape, B[t].shape))
    sa = [stats(A[t], x0, y0, x1, y1) for t in common]
    sb = [stats(B[t], x0, y0, x1, y1) for t in common]
    ma = np.mean([s[0] for s in sa], axis=0)
    mb = np.mean([s[0] for s in sb], axis=0)
    d = float(np.mean(np.abs(ma - mb)))
    # тон-расхождение — ПО ПАРАМ (один и тот же момент матча), а не по
    # средним гистограммам: усреднение сглаживает именно те сдвиги,
    # которые метрика обязана ловить
    tone = float(np.mean([np.abs(x[6] - y[6]).sum() * 100.0
                          for x, y in zip(sa, sb)]))
    # gross% — доля пикселей региона с |Δ|>32 хотя бы по одному каналу. Средние
    # по региону могут совпадать при полностью разной картинке (плюс/минус
    # компенсируются), поэтому без этой метрики |D| вводит в заблуждение
    gross = float(np.mean([np.mean(np.abs(A[t] - B[t])[y0:y1, x0:x1].max(axis=2) > 32)
                           for t in common])) * 100.0
    return dict(meanA=ma.mean(), meanB=mb.mean(),
                lumA=np.mean([s[1] for s in sa]), lumB=np.mean([s[1] for s in sb]),
                blkA=np.mean([s[2] for s in sa]), blkB=np.mean([s[2] for s in sb]),
                d=d, tone=tone, gross=gross,
                detA=np.mean([s[7] for s in sa]), detB=np.mean([s[7] for s in sb]),
                brA=np.mean([s[5] for s in sa]), brB=np.mean([s[5] for s in sb]),
                uniqA=np.mean([s[4] for s in sa]), uniqB=np.mean([s[4] for s in sb]),
                wA=np.mean([s[3] for s in sa]), wB=np.mean([s[3] for s in sb]))


def main():
    A, B = series(sys.argv[1]), series(sys.argv[2])
    common = sorted(set(A) & set(B))
    if not common:
        sys.exit("нет общих снимков: шаблоны %r / %r не пересеклись" % (sys.argv[1], sys.argv[2]))
    print("секунды: A=%s B=%s общие=%s" % (sorted(A), sorted(B), common))
    hdr = "%-14s %9s %9s %9s %9s %9s %9s" % ("region", "meanD_A", "meanD_B",
                                             "lumA", "lumB", "blackA%", "blackB%")
    print(hdr)
    mAB = {}
    for name, x0, y0, x1, y1 in REGIONS:
        m = mAB[name] = pair_stats(A, B, common, x0, y0, x1, y1)
        print("%-14s %9.1f %9.1f %9.1f %9.1f %9.2f %9.2f  |D|=%.1f gross=%.1f%% "
              "toneL1=%.1f detA=%.2f detB=%.2f brA=%+.1f brB=%+.1f uniqA=%d uniqB=%d wA=%.2f wB=%.2f"
              % (name, m["meanA"], m["meanB"], m["lumA"], m["lumB"],
                 m["blkA"], m["blkB"], m["d"], m["gross"], m["tone"],
                 m["detA"], m["detB"], m["brA"], m["brB"],
                 m["uniqA"], m["uniqB"], m["wA"], m["wB"]))

    # ТРЕТИЙ шаблон — повторный прогон той же стороны, что и A: это ПОЛОСА ШУМА.
    # Без неё |D| региона неотличим от разницы двух разных матчей (камера,
    # состав юнитов, фаза боя): замер 2026-10-21 — у одного и того же региона
    # hud_top |D| в одной серии 5,3, в другой 26,2 при одном и том же exe.
    if len(sys.argv) > 3:
        C = series(sys.argv[3])
        common2 = sorted(set(A) & set(C))
        print("\nшум: A=%s C=%s общие=%s" % (sorted(A), sorted(C), common2))
        print("%-14s %9s %9s %7s %9s %9s %7s %9s %9s" % ("region", "|D|сигн", "|D|шум",
                                                 "сигн/шум", "grossсигн", "grossшум",
                                                 "тон-отн", "detA", "detB"))
        for name, x0, y0, x1, y1 in REGIONS:
            n = pair_stats(A, C, common2, x0, y0, x1, y1)
            s = mAB[name]
            print("%-14s %9.1f %9.1f %7.2f %9.1f %9.1f %7.2f %9.2f %9.2f  %s"
                  % (name, s["d"], n["d"],
                     s["d"] / max(0.5, n["d"]), s["gross"], n["gross"],
                     s["tone"] / max(1.0, n["tone"]), s["detA"], s["detB"],
                     "СИГНАЛ" if s["d"] > 2 * n["d"] else
                     ("тон-сигнал" if s["tone"] > 2 * n["tone"] else "в шуме")))


if __name__ == '__main__':
    main()
