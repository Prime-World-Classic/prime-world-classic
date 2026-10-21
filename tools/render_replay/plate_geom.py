#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""plate_geom.py — разбор вывода-плашки из записи PWRD: вершины, матрицы, проекция.

Зачем. Белая плашка 330x132 в (29,443)-(359,575) появляется только в Vulkan.
По трассам состояние (бленд, глубина, трафарет, вьюпорт, scissor) и вершины
совпадают с D3D9, поэтому остаётся геометрия: как SPIR-V трактует WORLD/VIEW.
Скрипт берёт вывод из PWRD-записи (по отпечатку SPIR-V и/или bbox вершин),
печатает вершины, WORLD (c0..c3), VIEW (c4..c7) и прямоугольник после
проекции в ДВУХ конвенциях:

  col  = M * v   (HLSL mul(matrix, vector); перевод fxc/DXC)
  row  = v * M   (строковый вектор; D3DX-раскладка, перевод = транспонирование)

Запуск: (ulimit -v 6000000; timeout 300 python3 plate_geom.py ЗАПИСЬ.pwrd
         [--hash spvVs:hash,spvPs:hash] [--bbox x0,y0,x1,y1] [--tol 3] [--max 4])
"""

import argparse
import struct
import sys

import parse_pwrd

FNV_OFF = 1469598103934665603
FNV_MUL = 1099511628211


def fnv(b):
    h = FNV_OFF
    for x in b:
        h ^= x
        h = (h * FNV_MUL) & 0xFFFFFFFFFFFFFFFF
    return h


def pos_attr(layouts, lidx):
    l = layouts.get(lidx)
    if not l:
        return None
    for a in l['attrs']:
        if str(a['name']).startswith('POSITION'):
            return a
    return None


def verts(d, buffers, layouts):
    a = pos_attr(layouts, d['layout'])
    if not a:
        return []
    if a['bufferIndex'] >= len(d['streams']):
        return []
    bid, off, stride = d['streams'][a['bufferIndex']]
    buf = buffers.get(bid)
    if not buf:
        return []
    data = buf['data']
    stride = a['elementStride'] or stride
    out = []
    for k in range(d['vtx']):
        p = off + (d['startVtx'] + k) * stride + a['offset']
        if p + 8 > len(data):
            break
        out.append(struct.unpack_from('<ff', data, p))
    return out


def proj(cv, x, y, vp, conv):
    v = [x, y, 0.0, 1.0]
    def mul(m, vec):
        if conv == 'col':          # M * v
            return [sum(m[4 * i + j] * vec[j] for j in range(4)) for i in range(4)]
        return [sum(m[4 * i + j] * vec[i] for i in range(4)) for j in range(4)]  # v * M
    q = mul(cv[0:16], v)
    r = mul(cv[16:32], q)
    if r[3] == 0.0:
        return None
    px = (r[0] / r[3] + 1.0) * 0.5 * vp[2] + vp[0]
    py = (1.0 - r[1] / r[3]) * 0.5 * vp[3] + vp[1]
    return px, py, r[3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pwrd')
    ap.add_argument('--hash', default='', help='spvVs:HASH,spvPs:HASH (FNV-1a 64, hex)')
    ap.add_argument('--bbox', default='', help='x0,y0,x1,y1 — окно по bbox вершин')
    ap.add_argument('--tol', type=float, default=3.0)
    ap.add_argument('--max', type=int, default=4)
    a = ap.parse_args()

    want = None
    if a.hash:
        vs, ps = a.hash.split(',')
        want = (int(vs.split(':')[1], 16), int(ps.split(':')[1], 16))
    win = None
    if a.bbox:
        win = [float(x) for x in a.bbox.split(',')]

    hdr, shaders, buffers, textures, layouts, draws, end = parse_pwrd.parse(a.pwrd)
    sh = {s['id']: s for s in shaders.values()}
    shown = 0
    for d in draws:
        if shown >= a.max:
            break
        vv = verts(d, buffers, layouts)
        if not vv:
            continue
        xs = [p[0] for p in vv]
        ys = [p[1] for p in vv]
        bb = (min(xs), min(ys), max(xs), max(ys))
        if win and not (abs(bb[0] - win[0]) <= a.tol and abs(bb[1] - win[1]) <= a.tol
                        and abs(bb[2] - win[2]) <= a.tol and abs(bb[3] - win[3]) <= a.tol):
            continue
        svs, sps = sh.get(d['vs']), sh.get(d['ps'])
        hv = fnv(svs['spv']) if svs else 0
        hp = fnv(sps['spv']) if sps else 0
        if want and (hv, hp) != want:
            continue
        shown += 1
        print('вывод n=%d ps=%d vs=%d lay=%d rt=%dx%d vp=%d,%d %dx%d prim=%d vtx=%d'
              % (d['n'], d['ps'], d['vs'], d['layout'], d['rtW'], d['rtH'],
                 d['vpX'], d['vpY'], d['vpW'], d['vpH'], d['prim'], d['vtx']))
        print('  spvVs=%d:%016x spvPs=%d:%016x  entry=%r/%r'
              % (len(svs['spv']) if svs else 0, hv, len(sps['spv']) if sps else 0, hp,
                 svs['entry'] if svs else None, sps['entry'] if sps else None))
        print('  bbox=(%.2f,%.2f)-(%.2f,%.2f) вершины: %s'
              % (bb[0], bb[1], bb[2], bb[3],
                 ' '.join('(%.2f,%.2f)' % p for p in vv[:6])))
        cv = d['cVs']
        for i in range(8):
            print('  c%-2d = (%.6f, %.6f, %.6f, %.6f)'
                  % (i, cv[4 * i], cv[4 * i + 1], cv[4 * i + 2], cv[4 * i + 3]))
        print('  c20 = %s  c21 = %s'
              % (tuple(round(v, 4) for v in cv[80:84]), tuple(round(v, 4) for v in cv[84:88])))
        vp = (d['vpX'], d['vpY'], d['vpW'], d['vpH'])
        for conv in ('col', 'row'):
            pts = [proj(cv, x, y, vp, conv) for x, y in vv[:4]]
            pts = [p for p in pts if p]
            if pts:
                print('  proj[%s]: %s' % (conv, ' '.join('(%.1f,%.1f w=%.4g)' % p for p in pts)))
        st = d['state']
        print('  state: %s' % {k: st[k] for k in sorted(st)})
        print('  samplers: %s' % (d['samplers'][:4],))
    if not shown:
        print('нет выводов под фильтр (всего выводов %d)' % len(draws))


if __name__ == '__main__':
    main()
