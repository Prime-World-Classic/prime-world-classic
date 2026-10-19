#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""inspect_draw.py — разбор одного вывода из PWRD «по существу»:
матрицы из c-банка, вершины VB, срез буфера экземпляров.

Нужен чтобы ответить на вопрос «почему кадр не осмысленный»: если world/view/proj
в c-банке не похожи на матрицы (или нули) — виновата доставка констант, если
матрицы правильные, а вершины/экземпляры нет — виноват вершинный путь.

  ./inspect_draw.py <запись.pwrd> [--draw N] [--bank] [--vb] [--inst]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_pwrd import parse  # noqa: E402


def f32(bs, o=0):
    return struct.unpack_from("<f", bs, o)[0]


def looks_like_row(v):
    return all(-1e6 < x < 1e6 for x in v)


def scan_matrices(bank, tag):
    """Ищем 4x4-блоки (4 строки по 4 float) с признаками аффинной проекции:
    последняя строка (0,0,0,1) или (0,0,0,±1), и ненулевая диагональ."""
    hits = []
    for i in range(0, len(bank) - 16):
        m = bank[i:i + 16]
        if all(x == 0.0 for x in m):
            continue
        r3 = m[12:16]
        if abs(r3[3]) < 0.5 or abs(r3[3]) > 2.0:
            continue
        if max(abs(x) for x in r3[:3]) > 0.01:
            continue
        # признак проекции: m[2][2] != 0 и m[3][2] != 0 (перспектива) или
        # ортогональная: m[2][2] != 0, m[3][2] == 0
        det = 0.0
        hits.append((i, m))
        if len(hits) >= 12:
            break
    print("%s: 4x4-блоков найдено %d" % (tag, len(hits)))
    for i, m in hits[:6]:
        print("   c%-4d %s" % (i, " | ".join(
            ",".join("%.4g" % x for x in m[r * 4:r * 4 + 4]) for r in range(4))))
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pwrd")
    ap.add_argument("--draw", type=int, default=0)
    ap.add_argument("--bank", action="store_true")
    ap.add_argument("--vb", action="store_true")
    ap.add_argument("--inst", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--ne", action="store_true", help="первый вывод с ненулевым VB")
    a = ap.parse_args()
    hdr, shaders, buffers, textures, layouts, draws, endpos = parse(a.pwrd)
    print("выводов=%d буферов=%d" % (len(draws), len(buffers)))
    if a.ne:
        for i, dr in enumerate(draws):
            bid = dr["streams"][0][0]
            b = buffers.get(bid)
            if b and any(x != 0 for x in b["data"][:4096]):
                a.draw = i
                print("первый вывод с ненулевым vb0: #%d" % i)
                break
    dr = draws[a.draw]
    print("== вывод #%d: vtx=%d prim=%d inst=%d lay=%s" %
          (a.draw, dr["vtx"], dr["primCount"], dr["instCount"], dr["layout"]))
    lo = layouts.get(dr["layout"])
    if lo:
        for i, e in enumerate(lo["elems"]):
            print("   элм%-2d stream=%-11d off=%-3d type=%-3d usage=%-4d ui=%d" %
                  (i, e[0], e[1], e[2], e[3], e[4]))
    if a.bank or a.all:
        scan_matrices(dr["cVs"], "VS c-банк")
        scan_matrices(dr["cPs"], "PS c-банк")
        nz = [(i, v) for i, v in enumerate(dr["cVs"]) if v != 0.0]
        print("VS банк: ненулевых %d из %d; первые: %s" %
              (len(nz), len(dr["cVs"]), ", ".join("c%d=%.4g" % (i, v) for i, v in nz[:16])))
    if a.vb or a.all:
        for slot, (bid, off, stride) in enumerate(dr["streams"]):
            if bid >= 0x80000000:
                continue
            b = buffers.get(bid)
            if not b:
                print("   vb%d: буфер #%d отсутствует" % (slot, bid))
                continue
            blob = b["data"]
            print("   vb%d: буфер #%d размер=%d off=%d stride=%d" %
                  (slot, bid, len(blob), off, stride))
            head = blob[off:off + max(stride, 1) * 4]
            print("      как float: %s" % ", ".join("%.4g" % f32(head, 4 * k)
                                                   for k in range(min(16, len(head) // 4))))
            print("      как uint : %s" % ", ".join(str(int.from_bytes(head[4 * k:4 * k + 4], "little"))
                                                    for k in range(min(8, len(head) // 4))))
    if a.inst or a.all:
        ib = dr["instBuf"]
        if ib >= 0:
            b = buffers.get(ib)
            blob = b["data"] if b else b""
            off, stride = dr["instOff"], dr["instStride"]
            print("   inst: буфер #%d размер=%d off=%d stride=%d count=%d" %
                  (ib, len(blob), off, stride, dr["instCount"]))
            for n in range(min(3, dr["instCount"])):
                s = blob[off + n * stride: off + (n + 1) * stride]
                print("      экз.%d float: %s" % (n, ", ".join("%.4g" % f32(s, 4 * k)
                                                               for k in range(len(s) // 4))))
        else:
            print("   inst: буфера нет (instSrc = kNoRes)")


if __name__ == "__main__":
    main()
