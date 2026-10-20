#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка нумерации SPIR-V binding'ов в контейнере .shd с регистрами D3D9 из исходника.

Зачем: seam (Src/Render/nvrhibackend.cpp) использует номер binding из SPIR-V
КАК D3D9-слот сэмплера (d.texBound[it.binding]; комментарий «SetTexture(N, tex)
кладёт текстуру прямо в binding N»). DXC же нумерует binding'ы по порядку
объявления неиспользуемых ресурсов, и при перегенерации корпуса нумерация
съезжает (замер: FullScreenMaterial, 14 байт в SPIR-V-слое -> красный экран).
Скрипт печатает, какие имена на каких binding'ах лежат в каждом варианте, и
рядом — регистры из .hlsl, чтобы видеть расхождение.

Запуск: bind_check.py <файл.shd> [--src <Material.hlsl>] [--only N] [--all]
"""

import argparse
import os
import re
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(_HERE, "..", "spirv_probe"))
import spirv_iface  # noqa: E402
import shim         # noqa: E402  (expand_reg/slot_macros: GlobalMasks.h -> 's10')


def iter_modules(path):
    """[(index, 'vs'|'ps', blob)] для каждого SPIR-V-модуля контейнера."""
    data = open(path, "rb").read()
    tail = data[-16:]
    if tail[:4] != b"SHD2" or tail[12:16] != b"SHD2":
        sys.exit("%s: не v2 (нет трэйлера SHD2)" % path)
    v2off, _ver = struct.unpack_from("<II", tail, 4)
    tag, ver, v1size, tcnt = struct.unpack_from("<4sIII", data, v2off)
    if tag != b"V2H\0":
        sys.exit("%s: плохая метка заголовка %r" % (path, tag))
    print("%s: v1Size=%d целей=%d" % (os.path.basename(path), v1size, tcnt))
    # сразу за заголовком — таблица целей: (tag, off, cnt) по 12 Б; сами секции
    # лежат ОТДЕЛЬНО (off — абсолютный offset в файле, см. mkshd2.py:528-536)
    tables = [struct.unpack_from("<III", data, v2off + 16 + 12 * k) for k in range(tcnt)]
    out = []
    for tag_i, off, cnt in tables:
        stag, scnt = struct.unpack_from("<II", data, off)
        assert stag == tag_i and scnt == cnt, \
            "%s: таблица цели и секция не бьются (%x/%x, %d/%d)" % (path, tag_i, stag, cnt, scnt)
        ents = [struct.unpack_from("<IIIII", data, off + 8 + 20 * k) for k in range(scnt)]
        base = off + 8 + 20 * scnt
        for index, voff, vsz, poff, psz in ents:
            # смещения в записи — абсолютные (emit(base) с base = offset секции)
            if vsz:
                out.append((index, "vs", data[voff:voff + vsz]))
            if psz:
                out.append((index, "ps", data[poff:poff + psz]))
        print("  цель %r вариантов=%d блобы с %d" %
              (struct.pack("<I", stag).decode("latin1"), scnt, base))
    return out


RE_REG = re.compile(r"^\s*(?:texture2D|sampler2D|samplerCUBE|sampler|textureCube)"
                    r"[^;]*?\b(\w+)\s*(?::\s*register\s*\(\s*([^)]*?)\s*\))?\s*;", re.M)
def src_regs(path):
    """имя ресурса -> строка регистра из исходника (D3D9-слот). Макросы
    (WARFOGSAMPLERINDEX и др.) раскрываются тем же кодом, что и в прослойке,
    иначе проверка расходится с тем, что видит DXC."""
    txt = open(path, "r", encoding="utf-8", errors="replace").read()
    res = {}
    for name, reg in RE_REG.findall(txt):
        res[name] = shim.expand_reg(reg)
    return res


RE_OPNAME = re.compile(r'^\s*OpName %(\S+) "([^"]*)"')
RE_BIND = re.compile(r"^\s*OpDecorate %(\S+) Binding (\d+)")
RE_SET = re.compile(r"^\s*OpDecorate %(\S+) DescriptorSet (\d+)")
RE_VAR = re.compile(r"^\s*%(\d+) = OpVariable %(\S+) (\w+)")
RE_PTR = re.compile(r"^\s*%(\S+) = OpTypePointer (\w+) (%\S+)")
RE_IMG = re.compile(r"^\s*%(\S+) = OpTypeImage %\S+ (\w+)")
RE_SIMG = re.compile(r"^\s*%(\S+) = OpTypeSampledImage %(\S+)")


def descriptors(txt):
    """[(set, binding, name, kind)] из листинга SPIR-V.

    parse_iface отдаёт только Location'ы вершинных атрибутов; binding'ы (главное
    в этой проверке) разбираются здесь: DXC вешает Binding/DescriptorSet на сам
    OpVariable, поэтому decorates собираются по id переменной.
    """
    names, binds, sets, kinds, ptrs = {}, {}, {}, {}, {}
    for line in txt.split("\n"):
        m = RE_OPNAME.match(line)
        if m:
            names[m.group(1)] = m.group(2)
            continue
        m = RE_BIND.match(line)
        if m:
            binds[m.group(1)] = int(m.group(2))
            continue
        m = RE_SET.match(line)
        if m:
            sets[m.group(1)] = int(m.group(2))
            continue
        m = RE_PTR.match(line)
        if m:
            ptrs[m.group(1)] = m.group(3)
            continue
        m = RE_IMG.match(line)
        if m:
            kinds[m.group(1)] = "img/" + m.group(2)
            continue
        m = RE_SIMG.match(line)
        if m:
            kinds[m.group(1)] = "sampled"
            continue
        m = RE_VAR.match(line)
        if m:
            var, ptype, store = m.group(1), m.group(2), m.group(3)
            kinds["%" + var] = kinds.get(ptrs.get(ptype, "?"), store)
    out = [(sets.get(v, 0), b, names.get(v, v), kinds.get(v, "?"))
           for v, b in binds.items()]
    out.sort()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shd")
    ap.add_argument("--src", help=".hlsl материала (для колонок с регистрами)")
    ap.add_argument("--only", type=int, help="только этот индекс варианта")
    ap.add_argument("--all", action="store_true", help="не сворачивать одинаковые наборы")
    ap.add_argument("--check", action="store_true",
                    help="печатать только расхождения: binding != номер регистра "
                         "из исходника и коллизии (set,binding) внутри варианта")
    a = ap.parse_args()

    regs = {}
    src = a.src
    if not src:
        cand = os.path.join(_HERE, "..", "..", "pw", "branches", "r1117",
                            "Src", "Render", "Shaders",
                            os.path.basename(a.shd)[:-4] + ".hlsl")
        src = cand if os.path.isfile(cand) else None
    if src:
        regs = src_regs(src)
    seen = {}
    for index, kind, blob in iter_modules(a.shd):
        if a.only is not None and index != a.only:
            continue
        txt, err = spirv_iface.disasm(blob)
        if txt is None:
            print("вариант %d %s: disasm не удался: %s" % (index, kind, err[:200]))
            continue
        items = descriptors(txt)
        if a.check:
            seen_bd = {}
            for s, b, n, k in items:
                reg = regs.get(n)
                m = re.match(r"([tsub])\s*(\d+)$", reg or "")
                if m and int(m.group(2)) != b:
                    print("!! вариант %d %s: %s reg=%s -> binding %d (ожидался %s%s)"
                          % (index, kind, n, reg, b, m.group(1), m.group(2)))
                if (s, b) in seen_bd:
                    print("!! вариант %d %s: коллизия set%d binding%d — %s и %s"
                          % (index, kind, s, b, seen_bd[(s, b)], n))
                else:
                    seen_bd[(s, b)] = n
            continue
        key = (kind, tuple(items))
        if key in seen and not a.all:
            seen[key].append(index)
            continue
        seen.setdefault(key, [index])
        parts = []
        for s, b, n, k in items:
            tag = "<reg %s>" % regs[n] if n in regs else ""
            parts.append("s%d:b%d=%s(%s)%s" % (s, b, n, k, tag))
        print("  вариант %d %s: %s" % (index, kind, " ".join(parts)))
    groups = {}
    for (kind, items), idxs in seen.items():
        groups.setdefault(kind, {}).setdefault(tuple(items), []).extend(idxs)
    for kind, g in groups.items():
        for items, idxs in g.items():
            if len(idxs) > 1:
                print("  (%s: одинаковый набор у %d вариантов, первые %s)" %
                      (kind, len(idxs), idxs[:6]))
    if regs:
        print("регистры из исходника: %s" %
              ", ".join("%s=%s" % (k, v) for k, v in sorted(regs.items())))


if __name__ == "__main__":
    main()
