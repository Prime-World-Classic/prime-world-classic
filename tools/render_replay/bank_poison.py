#!/usr/bin/env python3
"""Пересечение «регистры, которые шейдер реально читает» с «регистрами-мусором».

Смысл: в прогоне 2026.10.17 (vk_rec7.pwrd) хвост c-банка содержал указатели
Wine (0x0000776fe54bf5b0 -> как float -6.02e+22 / 4.28e-41), потому что
m_constShadow выделялся без обнуления (Src/Render/nvrhibackend.cpp:1324).
Сам по себе мусор в неиспользуемых регистрах безвреден; важен только случай,
когда шейдер обращается к регистру с мусором: тогда арифметика даёт NaN или
огромное значение, а Vulkan при конвертации в UNORM насыщает NaN/отрицательное
в 0 -> ровно чёрный RGB при корректной альфе.

Запуск: bank_poison.py [ФАЙЛ.pwrd]
"""
import collections
import math
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_pwrd import parse

_dis_cache = {}


def dis(spv):
    key = hash(spv)
    if key in _dis_cache:
        return _dis_cache[key]
    with tempfile.NamedTemporaryFile(suffix='.spv', delete=False) as f:
        f.write(spv)
        path = f.name
    try:
        txt = subprocess.run(['spirv-dis', path], capture_output=True,
                             text=True).stdout
    finally:
        os.unlink(path)
    _dis_cache[key] = txt
    return txt


def toks(line):
    return [t for t in line.split() if t != '=']


def bank_layout(spv):
    """(member -> (offset, слов_типа)) для uniform-структуры банка + читаемые индексы."""
    txt = dis(spv)
    # размеры скалярных/векторных типов в словах
    size = {'%float': 1, '%int': 1, '%uint': 1, '%bool': 1, '%v2float': 2,
            '%v3float': 3, '%v4float': 4, '%v2int': 2, '%v3int': 3, '%v4int': 4}
    types = {}
    for line in txt.split('\n'):
        t = toks(line)
        if not t or not t[0].startswith('%'):
            continue
        if 'OpTypeFloat' in t:
            types[t[0]] = 1
        elif 'OpTypeInt' in t:
            types[t[0]] = 1
        elif 'OpTypeVector' in t:
            k = t.index('OpTypeVector')
            types[t[0]] = types.get(t[k + 1], 1) * int(t[k + 2])
        elif 'OpTypeMatrix' in t:
            k = t.index('OpTypeMatrix')
            types[t[0]] = types.get(t[k + 1], 1) * int(t[k + 2])
        elif 'OpTypeArray' in t:
            k = t.index('OpTypeArray')
            n = int(t[k + 2][5:]) if t[k + 2].startswith('%int_') else \
                types.get(t[k + 2], 1)
            types[t[0]] = types.get(t[k + 1], 1) * n
    structs = {}
    for line in txt.split('\n'):
        t = toks(line)
        if 'OpTypeStruct' in t:
            k = t.index('OpTypeStruct')
            structs[t[0]] = t[k + 1:]
    offs = collections.defaultdict(dict)
    for line in txt.split('\n'):
        t = toks(line)
        if 'OpMemberDecorate' in t:
            k = t.index('OpMemberDecorate')
            if len(t) > k + 4 and t[k + 3] == 'Offset':
                offs[t[k + 1]][int(t[k + 2])] = int(t[k + 4])
    # какие члены трогает OpAccessChain по bank-переменной
    used = collections.defaultdict(set)
    for line in txt.split('\n'):
        t = toks(line)
        if 'OpAccessChain' in t:
            k = t.index('OpAccessChain')
            idxs = [int(m.group(1)) for a in t[k + 3:]
                    if (m := re.match(r'^%int_(\d+)$', a))]
            if idxs:
                used[t[k + 2]].add(idxs[0])
    out = {}
    for var, idxs in used.items():
        # тип переменной -> структура
        for line in txt.split('\n'):
            t = toks(line)
            if var in t and 'OpVariable' in t:
                k = t.index('OpVariable')
                ptr = t[k + 1]
                for l2 in txt.split('\n'):
                    t2 = toks(l2)
                    if t2 and t2[0] == ptr and 'OpTypePointer' in t2:
                        st = t2[t2.index('OpTypePointer') + 2]
                        if st in structs and st in offs:
                            out[st] = idxs
    # размеры членов
    res = {}
    for st, idxs in out.items():
        mem = structs[st]
        for mi in idxs:
            if mi >= len(mem):
                continue
            res[mi] = (offs[st][mi], types.get(mem[mi], 4))
    return res


def is_poison(v):
    if v == 0.0:
        return False
    a = abs(v)
    return a > 1e10 or a < 1e-30 or math.isnan(v) or math.isinf(v)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else \
        '/home/rekon/PW/wine-client/Game/Bin/vk_rec7.pwrd'
    hdr, shaders, buffers, textures, layouts, draws, end = parse(path)
    lay_cache = {}
    hits = collections.Counter()
    per_draw = 0
    for dr in draws:
        bad_here = []
        for stage, key, bank in (('PS', 'ps', dr['cPs']), ('VS', 'vs', dr['cVs'])):
            spv = shaders.get(dr[key], {}).get('spv')
            if not spv:
                continue
            if spv not in lay_cache:
                lay_cache[spv] = bank_layout(spv)
            for mi, (off, words) in lay_cache[spv].items():
                reg = off // 16
                for r in range(reg, min(256, reg + max(1, words // 4))):
                    if any(is_poison(bank[r * 4 + j]) for j in range(4)):
                        hits[(stage, r)] += 1
                        bad_here.append('%s c%d' % (stage, r))
        if bad_here:
            per_draw += 1
    print('выводов: %d, из них с мусором в ЧИТАЕМЫХ регистрах: %d'
          % (len(draws), per_draw))
    print('топ регистров:')
    for (stage, r), n in hits.most_common(20):
        print('   %s c%-4d выводов %d' % (stage, r, n))


if __name__ == '__main__':
    main()
