#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Поиск вывода, который рисует белую плашку 330x132 в (29,443)-(359,575).

Плашка найдена в GPU-снимке клиентской текстуры 1024x768 (см. раздел плана
«Охота за белой плашкой»), но геометрический поиск по записям PWRD её не
находил: проверялись неправильные прямоугольники (неверно восстановлен
пересчёт пиксельных координат UI в NDC).

Пересчёт для UI-шейдеров (замер по SPIR-V MainPass_VSMain, прогон 158):
    ndc_x = px*c23.x - 1,  ndc_y = 1 - py*c23.y,  w = c26, затем * матрица c0..c3
    (c23 = [2*0,75/1024, 2*0,75/768, ...] = [0,001465, 0,001953, ...])
    экран_x = (ndc_x+1)/2*1024 = px*0,75 ;  экран_y = (1-ndc_y)/2*768 = py*0,75
то есть для пиксельных координат UI итог — просто масштаб 0,75 без отражения.
Для мировых выводов позиции уже в NDC: экран_x = (x+1)/2*1024, экран_y = (1-y)/2*768.

Запуск: panel_hunt.py ЗАПИСЬ.pwrd [--rect 29,443,359,575] [--tol 6] [--max 25]
"""
import sys, struct, argparse
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import parse_pwrd as P

FLOAT = {0: 1, 1: 2, 2: 3, 3: 4}   # EVertexElementType FLOAT1..FLOAT4
KNO = 0xFFFFFFFF


def positions(dr, lay, bufs):
    """Список (x,y) сырых позиций вывода (usage POSITION=0 / POSITIONT=9)."""
    if dr['layout'] not in lay:
        return []
    L = lay[dr['layout']]
    out = []
    for (slot, off, etype, usage, uidx) in L['elems']:
        if usage not in (0, 9) or etype not in FLOAT:
            continue
        if slot == KNO:
            # позиция в инстанс-потоке (D3D9 instance stream, слот 8): данные
            # идут не из VB, а из instBuf с instStride по одному записи на инстанс
            buf, vbOff, stride = dr.get('instBuf', KNO), dr.get('instOff', 0), dr.get('instStride', 0)
            reps = dr.get('instCount', 0)
            if not reps:
                continue
        else:
            reps = 0
        if slot != KNO:
            if slot >= len(dr['streams']):
                continue
            buf, vbOff, stride = dr['streams'][slot]
        if buf == KNO or buf not in bufs:
            continue
        data = bufs[buf].get('data') or b''
        if not data or stride <= 0:
            continue
        idxs = None
        if slot == KNO:
            # для инстанс-потока индекс VB неприменим: перебираем сами инстансы
            idxs = range(reps)
        elif dr.get('indexed') and dr.get('ib', KNO) != KNO and dr['ib'] in bufs:
            ib = bufs[dr['ib']].get('data') or b''
            w = 4 if dr.get('index32') else 2
            base = dr.get('startIdx', 0) * w
            start = dr.get('startVtx', 0)
            cnt = dr['primCount'] * 3 if dr['prim'] == 3 else dr['primCount'] + 2
            idxs = []
            for k in range(cnt):
                o = base + k * w
                if o + w > len(ib):
                    break
                idxs.append(int.from_bytes(ib[o:o + w], 'little') + start)
        else:
            # для неиндексных выводов число вершин берём из vtx: у TRIANGLESTRIP
            # (prim=4) их primCount+2, а не primCount*3 (замер 2026-10-20: 350
            # выводов из 2999 — стрипы, раньше их прямоугольник считался по
            # мусору за концом буфера)
            idxs = range(dr['vtx'])
        nfloat = FLOAT[etype]
        for i in idxs:
            o = vbOff + i * stride + off
            if o + nfloat * 4 > len(data):
                continue
            v = struct.unpack_from('<%df' % nfloat, data, o)
            out.append((v[0], v[1]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rec')
    ap.add_argument('--rect', default='29,443,359,575')
    ap.add_argument('--tol', type=int, default=6)
    ap.add_argument('--max', type=int, default=25)
    ap.add_argument('--near', action='store_true',
                    help='печатать ближайшие по площади прямоугольника к плашке, а не только покрытые')
    a = ap.parse_args()
    px0, py0, px1, py1 = [int(x) for x in a.rect.split(',')]
    hdr, sh, bufs, tex, lay, draws, off = P.parse(a.rec)
    texById = tex
    hits = []
    for dr in draws:
        if (dr['rtW'], dr['rtH']) != (1024, 768):
            continue
        pts = positions(dr, lay, bufs)
        if not pts:
            continue
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        mx = max(max(abs(min(xs)), abs(max(xs))), abs(max(ys)), abs(min(ys)))
        if mx <= 2.5:            # NDC
            sx0, sx1 = (min(xs) + 1) * 512.0, (max(xs) + 1) * 512.0
            sy0, sy1 = (1 - max(ys)) * 384.0, (1 - min(ys)) * 384.0
            space = 'NDC'
        else:                   # пиксели UI -> экран с масштабом 0,75
            sx0, sx1 = min(xs) * 0.75, max(xs) * 0.75
            sy0, sy1 = min(ys) * 0.75, max(ys) * 0.75
            space = 'px*0,75'
        t = a.tol
        if not (sx1 > px0 - t and sx0 < px1 + t and sy1 > py0 - t and sy0 < py1 + t):
            continue
        ids = [s[0] for s in dr['samplers']]
        sizes = []
        for i in ids:
            tt = texById.get(i)
            sizes.append('%dx%d' % (tt['w'], tt['h']) if tt else '?')
        if a.near:
            area = (sx1 - sx0) * (sy1 - sy0)
            ref = float((px1 - px0) * (py1 - py0))
            hits.append((abs(area - ref) / ref, dr['n'], dr['vs'], dr['ps'], dr['prim'], dr['vtx'], space,
                         sx0, sy0, sx1, sy1, sizes, dr['state']))
            continue
        # жёсткий отбор: прямоугольник вершин должен ПОКРЫВАТЬСЯ плашкой
        # (с допуском) — мировые меши с огромным bbox отпадают сами
        if not (sx0 >= px0 - t and sx1 <= px1 + t and sy0 >= py0 - t and sy1 <= py1 + t):
            continue
        hits.append((dr['n'], dr['vs'], dr['ps'], dr['prim'], dr['vtx'], space,
                     sx0, sy0, sx1, sy1, sizes, dr['state']))
    print('выводов в 1024x768: %d, попаданий в (%d,%d)-(%d,%d): %d'
          % (sum(1 for d in draws if (d['rtW'], d['rtH']) == (1024, 768)),
             px0, py0, px1, py1, len(hits)))
    hits.sort(key=lambda h: h[0] if a.near else 0)
    for h in hits[:a.max]:
        if a.near:
            print('#%d vs=%d ps=%d prim=%d vtx=%d %s экран(%.0f,%.0f)-(%.0f,%.0f) Δплощадь=%.2f tex=%s'
                  % (h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[0], ' '.join(h[11][:6])))
        else:
            print('#%d vs=%d ps=%d prim=%d vtx=%d %s экран(%.0f,%.0f)-(%.0f,%.0f) tex=%s'
                  % h[:11])
        st = h[12] if a.near else h[11]
        print('    state: blend=%d alphaTest=%d/%d zWrite=%d cw=%X cull=%d' % (
            st['alphaBlendEnable'], st['alphaTestEnable'], st['alphaRef'],
            st['zWriteEnable'], st['colorWriteEnable'], st['cullMode']))


if __name__ == '__main__':
    main()
