#!/usr/bin/env python3
"""Разбор выводов, которые сэмплируют атлас шрифта (L8 2048x2048).

Зачем: надписи HUD в кадре Vulkan читаются как ряд тёмных квадратиков (замер
20.10, см. PLAN_NVRHI — раздел про незакрытые входы VS). Нужно понять, что именно
в этих выводах не так: примитивы, прямоугольник отсечения, глубина,
blending или координаты вершин.

Важно про кроп: позиции UI/шрифта — в пиксельных координатах (y сверху вниз),
но VS пересчитывает их в NDC с масштабом 0,75 (c23), так что кадр надо кропить
по пересчёту (x*0,75, y*0,75) — иначе кроп пустой и вывод о «текста нет» ложный.

Запись PWRD берётся из клиента (PW_VK_RECORD=... PW_VK_RECORD_AT=<сек>
PW_VK_RECORD_MAX=<выводов>). Позиции UI/шрифта — в пиксельных координатах,
y сверху вниз (в отличие от мира, где NDC).

Запуск: font_draws.py /tmp/rec157.pwrd [--tex WxH@seamFmt] [--max N]
"""
import argparse
import struct
from collections import Counter

import parse_pwrd as P

FLOAT_TYPES = {0: 1, 1: 2, 2: 3, 3: 4}      # VERTEXELEMENTTYPE_FLOAT1..4
USAGE_POSITION, USAGE_POSITIONT = 0, 9
PRIM = {1: "POINTLIST", 2: "LINELIST", 3: "LINESTRIP", 5: "TRIANGLELIST",
        6: "TRIANGLESTRIP", 7: "TRIANGLEFAN"}
BF = {}


def bbox(dr, layouts, bufs):
    """Прямоугольник вершин по CPU-зеркалу VB (POSITION/POSITIONT, float)."""
    lay = layouts.get(dr.get("layout"))
    if not lay:
        return None, 0
    pts = []
    for (stream, off, etype, usage, _ui) in lay.get("elems", []):
        if usage not in (USAGE_POSITION, USAGE_POSITIONT):
            continue
        comps = FLOAT_TYPES.get(etype)
        if not comps:
            continue
        sid, vbOff, stride = dr["streams"][stream] if stream < len(dr["streams"]) \
            else (P.KNO if hasattr(P, "KNO") else 0xFFFFFFFF, 0, 0)
        buf = bufs.get(sid)
        if not buf or not stride:
            continue
        base = vbOff + off
        step = 4 * comps
        for i in range(dr.get("vtx", 0)):
            p = base + i * stride
            if p + step > len(buf["data"]):
                break
            v = struct.unpack_from("<%df" % comps, buf["data"], p)
            pts.append((v[0], v[1]))
    if not pts:
        return None, 0
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (round(min(xs), 1), round(min(ys), 1), round(max(xs), 1), round(max(ys), 1)), len(pts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--tex", default="2048x2048@4", help="размер@seamFormat атласа")
    ap.add_argument("--max", type=int, default=12)
    a = ap.parse_args()

    wh, fmt = a.tex.split("@")
    w, h = (int(x) for x in wh.split("x"))
    fmt = int(fmt)

    hdr, shaders, buffers, textures, layouts, draws, _o = P.parse(a.file)
    ids = {t["id"] for t in textures.values()
           if t["w"] == w and t["h"] == h and t["seamFormat"] == fmt}
    print("атлас %s@%d: id %s" % (a.tex, fmt, sorted(ids)))
    for t in sorted(ids):
        d = textures[t]["data"] or b""
        print("  id %d mips %d blob %d Б ненулевых %d макс %d" %
              (t, textures[t]["mips"], len(d), sum(1 for b in d if b), max(d) if d else 0))

    hits = [d for d in draws if any(s[0] in ids for s in d["samplers"])]
    print("выводов с этим атласом: %d из %d" % (len(hits), len(draws)))
    cnt = Counter((d["vs"], d["ps"]) for d in hits)
    for (vs, ps), n in cnt.most_common(4):
        print("  vs %s / ps %s — %d" % (shaders.get(vs, {}).get("entry", vs),
                                        shaders.get(ps, {}).get("entry", ps), n))
    st = Counter()
    for d in hits:
        s = d.get("state") or {}
        st[(s.get("cullMode"), s.get("fillMode"), s.get("zEnable"),
            s.get("zWriteEnable"), s.get("zFunc"))] += 1
    print("состояние (cull,fill,z,zw,zfunc):")
    for k, n in st.most_common(6):
        print("  ", k, n)
    print("первые %d выводов:" % a.max)
    for d in hits[:a.max]:
        bb, npts = bbox(d, layouts, buffers)
        s = d.get("state") or {}
        print("  #%d %s primCount=%d vtx=%d vp=%d,%d %dx%d scissor=%s bbox=%s вершин=%d "
              "z=%s/%s f=%s cull=%s fill=%s" %
              (d["n"], PRIM.get(d["prim"], d["prim"]), d["primCount"], d["vtx"],
               d["vpX"], d["vpY"], d["vpW"], d["vpH"],
               (d["scissorValid"], d["scL"], d["scT"], d["scR"], d["scB"]), bb, npts,
               s.get("zEnable"), s.get("zWriteEnable"), s.get("zFunc"),
               s.get("cullMode"), s.get("fillMode")))


if __name__ == "__main__":
    main()
