#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""map_rvas.py — разбор стека крэша (@rltv@ ... PW_Game.exe <RVA>) по карте линкера.

map_sym.py разбирает ОДИН адрес и регуляркой под x86 (8 разрядов); этот инструмент
принимает сразу весь стек (x86 и x64: Rva+Base бывает 8 или 16 разрядов) и печатает
имя символа + смещение для каждого кадра.

Использование:
  Tools/WineClientBuild/map_rvas.py <map> --rvas A76051 126551 ...
  Tools/WineClientBuild/map_rvas.py <map> --log "<...>-exception.log"
  Tools/WineClientBuild/map_rvas.py <map> --log -            # stdin

Карта — та же, что пишет линковка клиента (build_client_wine.sh всегда, в CMake-сборке
только по PW_EXTRA_LINK_OPTS="/MAP"):
  ./build_client_cmake.sh --toolchain=vs2022 PW_EXTRA_LINK_OPTS=/MAP
ВНИМАНИЕ: карта привязана к образу — брать .map из той же сборки, что и exe на госте.
"""
import argparse
import bisect
import re
import sys

SYM_RE = re.compile(r'^ [0-9a-fA-F]{4}:[0-9a-fA-F]{4,16}\s+(\S+)\s+([0-9A-Fa-f]{8,16})\s+(.*)$')
BASE_RE = re.compile(r'Preferred load address is ([0-9A-Fa-f]+)')
# @rltv@ PW_Game.exe A76051  (0x7FF6423D6051)
RVA_RE = re.compile(r'@rltv@\s+([A-Za-z0-9_.\-]+\.exe)\s+([0-9A-Fa-f]{4,16})\b')


def load_map(path):
    addrs = []
    names = []
    pref = None
    with open(path, 'r', errors='replace') as f:
        for line in f:
            m = BASE_RE.search(line)
            if m:
                pref = int(m.group(1), 16)
                continue
            m = SYM_RE.match(line.rstrip('\r\n'))
            if not m:
                continue
            a = int(m.group(2), 16)
            lib = m.group(3).strip()
            # <absolute> (ссылки CRT) и нулевые записи в карте — не символы кода
            if a == 0 or lib.startswith('<absolute>'):
                continue
            addrs.append(a)
            names.append('%s  [%s]' % (m.group(1), lib))
    if not addrs:
        sys.exit('в %s нет секции "Publics by Value" (карта битая?)' % path)
    # база образа: x86-карта даёт Rva+Base уже с базой (0x400000 / 0x140000000);
    # брать минимум по реальным символам, «Preferred load address» — если есть
    base = min(addrs)
    if pref and pref <= base:
        base = pref
    order = sorted(range(len(addrs)), key=lambda i: addrs[i])
    addrs = [addrs[i] - base for i in order]
    names = [names[i] for i in order]
    return addrs, names, base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map')
    ap.add_argument('--rvas', nargs='*', default=[])
    ap.add_argument('--log', help='exception-лог гостя (или - для stdin)')
    ap.add_argument('--module', default='PW_GAME.EXE', help='имя модуля в логе (безразлично по регистру)')
    a = ap.parse_args()

    addrs, names, base = load_map(a.map)
    print('map: %d symbols, image base 0x%x' % (len(addrs), base), file=sys.stderr)

    rvas = [r for r in a.rvas if r]
    if a.log:
        data = sys.stdin.read() if a.log == '-' else open(a.log, 'r', errors='replace').read()
        for mod, rva in RVA_RE.findall(data):
            if mod.lower() != 'pw_game.exe' and mod.lower() != a.module.lower():
                continue
            rvas.append(rva)

    seen = set()
    for r in rvas:
        rv = int(r, 16)
        tag = ' (repeat)' if rv in seen else ''
        seen.add(rv)
        i = bisect.bisect_right(addrs, rv) - 1
        if i < 0:
            print('%-10s -> ? (RVA ниже карты)' % hex(rv))
        else:
            print('%-10s -> %s (+0x%x)%s' % (hex(rv), names[i], rv - addrs[i], tag))


if __name__ == '__main__':
    main()
