#!/usr/bin/env python3
"""Разбор адресов Wine-билда PW_Game.exe по карте линкера (PW_Game.map).

Wine-билд линкуется без /DEBUG, поэтому в cdb кадры выглядят как
`PW_Game+0xNNNN` / `0098ce9c` без имён. /MAP в линковку добавлен
(build_client.py), карта лежит в IntDir проекта PW_Game:
  Src/_ShippingSingleExe/PW_Game/PW_Game.map

Употребление:
  ./map_sym.py 0098ce9c 00af06a2            # VA из cdb (образ на 0x400000)
  ./map_sym.py --offset 0x58ce9c            # PW_Game+0x... из cdb
  ./map_sym.py --near 0098ce9c              # соседние символы
  ./map_sym.py --map <путь> ...
"""
import argparse
import bisect
import os
import re
import sys

DEFAULT_MAP = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "..", "Src", "_ShippingSingleExe", "PW_Game", "PW_Game.map")
# строка "Publics by Value": 0001:00401000  ?Sym@@YAXXZ  00401000 f  Foo.obj
RE = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(.*)$")


def load(path):
    syms = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = RE.match(line)
        if m:
            syms.append((int(m.group(4), 16), m.group(3), m.group(5).strip()))
    syms.sort()
    return syms


def resolve(syms, va, near=0):
    vas = [s[0] for s in syms]
    i = bisect.bisect_right(vas, va) - 1
    if i < 0:
        return "адрес %08X вне диапазона символов" % va
    out = []
    for k in range(max(0, i - near), min(len(syms), i + 1 + near)):
        a, name, lib = syms[k]
        mark = "  <== " if k == i else "      "
        out.append("%s%08X +0x%-4X %s   [%s]" % (mark, a, va - a if k == i else 0, name, lib))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addrs", nargs="+", help="VA (0098ce9c) или адрес вида 0x98ce9c")
    ap.add_argument("--map", default=DEFAULT_MAP)
    ap.add_argument("--offset", action="store_true", help="трактовать как смещение от базы 0x400000")
    ap.add_argument("--near", type=int, default=0, help="показать N соседей с обеих сторон")
    args = ap.parse_args()
    if not os.path.isfile(args.map):
        sys.exit("нет карты: %s (пересобрать с /MAP)" % args.map)
    syms = load(args.map)
    print("карта: %s (%d символов)" % (args.map, len(syms)))
    for s in args.addrs:
        v = int(s, 16)
        if args.offset or v < 0x10000:
            v += 0x400000
        print("\n== %s -> VA %08X ==" % (s, v))
        print(resolve(syms, v, args.near))


if __name__ == "__main__":
    main()
