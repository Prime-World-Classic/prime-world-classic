#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""region_draws.py — какие выводы PWRD попадают в прямоугольник экрана.

Нужен для разбора визуальных дефектов без реплеера: по снимку видно, ЧТО не так
(белая плашка, пустая миникарта), а по записи — КАКИМ выводом это нарисовано
(или не нарисовано): цель, вьюпорт/сциссор, примитивы, привязанные текстуры с
размером и форматом, ключевые состояния смешивания. Если в прямоугольнике нет ни
одного вывода — дефект в том, что вывод не выполняется; если выводы есть, а
текстуры равны dummy (1x1) или отсутствуют — дефект в загрузке/привязке.

Поля записей берёт parse_pwrd.parse(); форматы текстур — EBackendFormat
(Src/Render/renderbackend.h:105).

Запуск:
  region_draws.py ФАЙЛ.pwrd X0 Y0 X1 Y1 [--rt ШхВ] [--limit N] [--textures]
                 [--geom]   -- фильтр по ВЕРШИНАМ (POSITION/POSITIONT в буфере),
                             а не по вьюпорту/сциссору: UI-выводы идут с
                             вьюпортом на весь экран и без сциссора, поэтому
                             прямоугольник по вьюпорту ловит всё подряд (замер:
                             2499 попаданий из 2499 выводов). Для UI вершины и
                             так в пикселях экрана.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_pwrd import parse, KNO  # noqa: E402

# EBackendFormat (renderbackend.h:105) — тот же порядок пишет RecTextureId
BF = ['unknown', 'A8R8G8B8', 'X8R8G8B8', 'A8', 'L8', 'L16', 'R5G6B5', 'R16F',
      'A16B16G16R16F', 'R32F', 'G32R32F', 'A32B32G32R32F', 'DXT1', 'DXT2',
      'DXT3', 'DXT4', 'DXT5', 'D24S8']

PRIM = {0: "POINTLIST", 1: "LINELIST", 2: "LINESTRIP", 3: "TRIANGLELIST",
        4: "TRIANGLESTRIP", 5: "TRIANGLEFAN"}

# EVertexElementUsage (vertexelementusage.h): POSITION=0, POSITIONT=9
USAGE_POSITION, USAGE_POSITIONT = 0, 9
# EVertexElementType (vertexelementtype.h): FLOAT1..FLOAT4 = 0..3
FLOAT_TYPES = {0: 1, 1: 2, 2: 3, 3: 4}


def geom_hits(dr, layouts, bufs, a):
    """Сколько вершин вывода лежит в прямоугольнике экрана (по CPU-зеркалу VB)."""
    lay = layouts.get(dr.get("layout"))
    if not lay:
        return 0, None
    n = 0
    first = None
    for (stream, off, etype, usage, _ui) in lay.get("elems", []):
        if usage not in (USAGE_POSITION, USAGE_POSITIONT):
            continue
        comps = FLOAT_TYPES.get(etype)
        if not comps:
            continue
        sid, vbOff, stride = dr["streams"][stream] if stream < len(dr["streams"]) \
            else (KNO, 0, 0)
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
            if a.x0 <= v[0] <= a.x1 and a.y0 <= v[1] <= a.y1:
                n += 1
                if first is None:
                    first = (v[0], v[1])
    return n, first


def fmtname(f):
    return BF[f] if 0 <= f < len(BF) else str(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("x0", type=int)
    ap.add_argument("y0", type=int)
    ap.add_argument("x1", type=int)
    ap.add_argument("y1", type=int)
    ap.add_argument("--rt", default=None, help="фильтр цели, напр. 1024x768")
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--textures", action="store_true",
                    help="печатать привязанные текстуры каждого вывода")
    ap.add_argument("--geom", action="store_true",
                    help="фильтр по вершинам POSITION/POSITIONT, не по вьюпорту")
    a = ap.parse_args()

    rt = None
    if a.rt:
        w, h = a.rt.lower().split("x")
        rt = (int(w), int(h))

    # parse() -> (hdr, shaders, buffers, textures, layouts, draws, endpos)
    hdr, _sh, bufs, tex, lays, draws, _end = parse(a.file)
    if hdr.get("truncated"):
        print("файл обрезан: %s" % hdr["truncated"])

    print(f"выводов: {len(draws)}, текстур в дампе: {len(tex)}")

    hit = 0
    for dr in draws:
        if rt and (dr.get("rtW"), dr.get("rtH")) != rt:
            continue
        first = None
        if a.geom:
            nvh, first = geom_hits(dr, lays, bufs, a)
            if not nvh:
                continue
        else:
            if dr.get("scissorValid"):
                rx0, ry0, rx1, ry1 = dr["scL"], dr["scT"], dr["scR"], dr["scB"]
            else:
                rx0, ry0 = dr["vpX"], dr["vpY"]
                rx1, ry1 = rx0 + dr["vpW"], ry0 + dr["vpH"]
            if rx1 <= a.x0 or ry1 <= a.y0 or rx0 >= a.x1 or ry0 >= a.y1:
                continue
        hit += 1
        if hit > a.limit:
            continue
        st = dr.get("state", {})
        if a.geom:
            rect = "вершин в прямоуг.=%d первая=(%.1f,%.1f)" % (nvh, first[0], first[1])
        else:
            rect = "rect=(%d,%d)-(%d,%d)" % (rx0, ry0, rx1, ry1)
        print(f"#{dr['n']:6} цель {dr.get('rtW')}x{dr.get('rtH')}"
              f" {rect}"
              f" vp=({dr['vpX']},{dr['vpY']},{dr['vpW']}x{dr['vpH']})"
              f" scissor={'on' if dr.get('scissorValid') else 'off'}"
              f" {PRIM.get(dr.get('prim'), dr.get('prim'))}"
              f" idx={dr.get('indexed')} prim={dr.get('primCount')}"
              f" vtx={dr.get('vtx')} inst={dr.get('instCount')}"
              f" vs={dr.get('vs')} ps={dr.get('ps')}"
              f" blend={st.get('alphaBlendEnable')} z={st.get('zEnable')}"
              f" zW={st.get('zWriteEnable')} cw={st.get('colorWriteEnable'):#x}"
              f" alphaTest={st.get('alphaTestEnable')}")
        if a.textures:
            for i, s in enumerate(dr.get("samplers", [])):
                tid = s[0]
                if tid == KNO:
                    continue
                t = tex.get(tid)
                if not t:
                    print(f"    слот {i}: tex id {tid} (нет в дампе)")
                    continue
                print(f"    слот {i}: {t['w']}x{t['h']} {fmtname(t['seamFormat'])}"
                      f" mips={t['mips']}{' cube' if t.get('cube') else ''}"
                      f" байт={t.get('size')}")
    print(f"попаданий в прямоугольник ({a.x0},{a.y0})-({a.x1},{a.y1}): {hit}")


if __name__ == "__main__":
    main()
