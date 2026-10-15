#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка клиентских снимков кадра (auto_screenshot) — визуальный гейт рендер-этапов.

Снимок берёт сам клиент (`setvar auto_screenshot N` в `Profiles/autoexec.cfg` →
`<cwd процесса>/pw_auto_{bb,scene,crop}_N.png`; x64-стенд — `C:\\pw64\\Game\\Bin`),
поэтому оверлеи ОС (в т.ч. «Activate Windows» на VNC-кадре) в него не попадают.

Метрика: поканальная дельта max(|dR|,|dG|,|dB|).
  diff  — доля пикселей с дельтой > --tol (по умолчанию 8): общий «шум» кадра;
  gross — доля пикселей с дельтой > --gross (по умолчанию 64): именно она
          отличает регресс рендера от анимации сцены.

Полоса шума между ДВУМЯ прогонами одного и того же бинарника на WARP
(замеры 2026-10-14/15, план `docs/PLAN_NVRHI.md` §6): diff 4–7%, gross
0,13–0,35%. Внутри одного прогона (кадр N против N+100) gross доходит до ~2% —
это анимация (герой/эффекты), а не регресс. therefore: сравнивать надо
`bb_<N>` с эталоном из `refs/`, а `bb_<N>` против `bb_<N+100>` — только чтобы
убедиться, что буфер не залипший.

Признак регресса, а не анимации: gross распределён ПОЛОСАМИ (ряд/колонка
сетки по всей ширине) или зонами UI, а не одним кластером в сцене. `--grid`
печатает счётчики по сетке и bounding box грубых пикселей.

Зависимость: Pillow (`pip install pillow`).

Пример:
  python3 tools/render_shot/shot_diff.py \\
      tools/render_shot/refs/stage2-2026-10-15/pw_auto_bb_3000.png \\
      /tmp/autoshots_stepF/pw_auto_bb_3000.png --grid
"""

import argparse
import sys

from PIL import Image


def load(path):
    im = Image.open(path).convert("RGB")
    return im, im.load()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("ref")
    ap.add_argument("new")
    ap.add_argument("--tol", type=int, default=8, help="порог «отличается» (default 8)")
    ap.add_argument("--gross", type=int, default=64, help="порог «грубо» (default 64)")
    ap.add_argument("--grid", action="store_true", help="счётчики грубых по сетке + bbox")
    ap.add_argument("--gx", type=int, default=16, help="сетка по X (default 16)")
    ap.add_argument("--gy", type=int, default=12, help="сетка по Y (default 12)")
    ap.add_argument("--mask", help="записать бинарную маску грубых пикселей (PNG)")
    ap.add_argument(
        "--zones",
        default="ui_bottom=bottom:90;ui_top=top:60;ui_left=left:60;ui_right=right:60",
        help="полосы вида name=край:отступ (top|bottom|left|right)",
    )
    args = ap.parse_args()

    a_im, a = load(args.ref)
    b_im, b = load(args.new)
    if a_im.size != b_im.size:
        print("РАЗМЕРЫ РАЗНЫЕ: %s %s vs %s %s" % (args.ref, a_im.size, args.new, b_im.size))
        return 2
    w, h = a_im.size

    zones = []
    for spec in args.zones.split(";"):
        if not spec.strip():
            continue
        name, geom = spec.split("=")
        edge, inset = geom.split(":")
        inset = int(inset)
        if edge == "top":
            x0, x1, y0, y1 = 0, w, 0, inset
        elif edge == "bottom":
            x0, x1, y0, y1 = 0, w, h - inset, h
        elif edge == "left":
            x0, x1, y0, y1 = 0, inset, 0, h
        elif edge == "right":
            x0, x1, y0, y1 = w - inset, w, 0, h
        else:
            print("неизвестный край зоны: %s" % edge)
            return 2
        zones.append((name, x0, y0, x1, y1))

    grid = [[0] * args.gx for _ in range(args.gy)]
    zone_tot = {name: 0 for name, *_ in zones}
    zone_hit = {name: 0 for name, *_ in zones}
    for name, x0, y0, x1, y1 in zones:
        zone_tot[name] = (x1 - x0) * (y1 - y0)

    mask = Image.new("L", (w, h), 0)
    mpix = mask.load()

    n = w * h
    diff = gross = 0
    maxd = 0
    first = None
    minx = miny = 10**9
    maxx = maxy = -1
    for y in range(h):
        for x in range(w):
            pa = a[x, y]
            pb = b[x, y]
            d = max(abs(pa[0] - pb[0]), abs(pa[1] - pb[1]), abs(pa[2] - pb[2]))
            if d > maxd:
                maxd = d
            if d > args.tol:
                diff += 1
                for name, x0, y0, x1, y1 in zones:
                    if x0 <= x < x1 and y0 <= y < y1:
                        zone_hit[name] += 1
            if d > args.gross:
                gross += 1
                mpix[x, y] = 255
                if first is None:
                    first = (x, y, d)
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
                grid[(y * args.gy) // h][(x * args.gx) // w] += 1

    print("%s vs %s  size=%dx%d tol=%d gross>%d" % (args.ref, args.new, w, h, args.tol, args.gross))
    print("  diff: %.2f%%   gross: %.2f%%   maxdelta=%d   (всего пикселей %d)"
          % (100.0 * diff / n, 100.0 * gross / n, maxd, n))
    for name, *_ in zones:
        print("  %-9s diff %.2f%% из %d" % (name, 100.0 * zone_hit[name] / zone_tot[name], zone_tot[name]))
    print("  первый грубый: %s" % (first,))

    if gross:
        print("  bbox грубых: x %d..%d, y %d..%d" % (minx, maxx, miny, maxy))
        cell = (w // args.gx) * (h // args.gy)
        rows = [sum(row) for row in grid]
        cols = [sum(grid[y][x] for y in range(args.gy)) for x in range(args.gx)]
        wide = [i for i, v in enumerate(rows) if v > 0.6 * max(rows)]
        tall = [i for i, v in enumerate(cols) if v > 0.6 * max(cols)]
        print("  строки сетки (>60%% от максимума): %s" % wide)
        print("  колонки сетки (>60%% от максимума): %s" % tall)
        if args.grid:
            print("  сетка %dx%d (счётчики грубых, ячейка ~%d пикс.):" % (args.gx, args.gy, cell))
            for row in grid:
                print("   " + " ".join("%4d" % v if v else "    ." for v in row))
        # эвристика: полоса = один ряд/колонка сетки держит почти все грубые
        if rows and max(rows) > 0.5 * gross:
            print("  ПОХОЖЕ НА ПОЛОСУ: ряд %d держит %.0f%% грубых"
                  % (rows.index(max(rows)), 100.0 * max(rows) / gross))
        if cols and max(cols) > 0.5 * gross:
            print("  ПОХОЖЕ НА ПОЛОСУ: колонка %d держит %.0f%% грубых"
                  % (cols.index(max(cols)), 100.0 * max(cols) / gross))

    if args.mask:
        mask.save(args.mask)
        print("  маска: %s" % args.mask)
    return 0


if __name__ == "__main__":
    sys.exit(main())
