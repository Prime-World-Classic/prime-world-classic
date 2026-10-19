#!/usr/bin/env python3
"""Разбор c-банка боевого вывода: что PS реально читает из UBO.

Сопоставляет SPIR-V из записи PWRD с контейнером .shd (по sha1 блоба), берёт из
SPIR-V имена и смещения членов `pwCBankPS`/`pwCBankVS` (OpMemberDecorate Offset
= регистр*16) и печатает значения, записанные клиентом в этих регистрах.

Зачем: кадр чёрный при исправной геометрии и рабочих текстурах (прогоны
2026-10-17: PS залитый константой = 99,8% экрана, PS = выборка текстуры =
видны цвета текстур). Значит чёрный даёт математика PS. Проверка показывает,
нулевые ли там данные или смещения членов не совпадают с тем, что пишет
клиент (SetPixelShaderConstants: байт = 16*регистр).

Употребление: bank_check.py --pwrd ФАЙЛ --shd КАТАЛОГ [--draw N] [--all]
"""
import argparse
import collections
import hashlib
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "shader_build"))

from parse_pwrd import parse                      # noqa: E402
import localign                                   # noqa: E402
import spirv_iface                                # noqa: E402


def spv_members(spv):
    """{смещение_в_байтах: имя_члена} для членов pwCBank{VS,PS} + имя банка."""
    txt, err = spirv_iface.disasm(spv)
    if txt is None:
        return {}, "disasm: %s" % err
    name_of = {}
    for line in txt.split("\n"):
        m = re.match(r"\s*%(\S+)\s*=\s*OpTypeStruct\b", line)
        if m:
            # OpName идёт РАНЬШЕ OpTypeStruct — не перетирать уже известное имя
            name_of.setdefault(m.group(1), None)
        m = re.match(r"\s*OpName\s+%(\S+)\s+\"([^\"]*)\"", line)
        if m:
            # OpName идёт ДО OpTypeStruct, поэтому имя пишем без условия
            # «структура уже встречена» (иначе имя членов банка теряется)
            name_of[m.group(1)] = m.group(2)
    # id структуры -> [(member, offset)]
    offs = collections.defaultdict(dict)
    for line in txt.split("\n"):
        m = re.match(r"\s*OpMemberName\s+%(\S+)\s+(\d+)\s+\"([^\"]*)\"", line)
        if m:
            offs[m.group(1)].setdefault(int(m.group(2)), {})["name"] = m.group(3)
        m = re.match(r"\s*OpMemberDecorate\s+%(\S+)\s+(\d+)\s+Offset\s+(\d+)",
                     line)
        if m:
            offs[m.group(1)].setdefault(int(m.group(2)), {})["off"] = int(m.group(3))
    res = {}
    for sid, members in offs.items():
        sname = name_of.get(sid) or ""
        if "pwCBank" not in sname:
            continue
        for k, v in members.items():
            if "off" in v:
                res[v["off"]] = "%s.%s" % (sname, v.get("name", "?%d" % k))
    return res, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pwrd", required=True)
    ap.add_argument("--shd", required=True)
    ap.add_argument("--draw")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--stage", default="ps", choices=["vs", "ps"])
    a = ap.parse_args()

    # индекс контейнеров: sha1(SPIR-V) -> (файл, индекс пары)
    idx = {}
    for fn in sorted(os.listdir(a.shd)):
        if not fn.endswith(".shd"):
            continue
        try:
            spv, _ = localign.read_spv_layer(os.path.join(a.shd, fn))
        except Exception:
            continue
        for i, (vs, ps) in (spv or {}).items():
            for st, b in enumerate((vs, ps)):
                if b:
                    idx[hashlib.sha1(b).hexdigest()] = (fn, i, st)

    hdr, shaders, buffers, textures, layouts, draws, _ = parse(a.pwrd)
    print("выводов %d, шейдеров %d" % (len(draws), len(shaders)))
    per = collections.Counter()
    for dr in draws:
        key = []
        for sid in (dr["vs"], dr["ps"]):
            sh = shaders.get(sid)
            if sh:
                key.append(idx.get(hashlib.sha1(sh["spv"]).hexdigest()))
            else:
                key.append(None)
        per[tuple(key)] += 1
    for k, n in per.most_common(12):
        print("  %-34s %-34s выводов %d"
              % ("%s#%s" % (k[0][0], k[0][1]) if k[0] else "?",
                 "%s#%s" % (k[1][0], k[1][1]) if k[1] else "?", n))

    want = [a.draw] if a.draw is not None else (
        sorted({str(dr["n"]) for dr in draws}) if a.all else [])
    for dn in want:
        dr = next((x for x in draws if str(x["n"]) == str(dn)), None)
        if dr is None:
            print("вывод %s не найден" % dn)
            continue
        sid = dr["ps"] if a.stage == "ps" else dr["vs"]
        sh = shaders.get(sid)
        if not sh:
            print("вывод %s: шейдер %s не записан" % (dn, sid))
            continue
        hit = idx.get(hashlib.sha1(sh["spv"]).hexdigest())
        print("\nвывод %s  %s: entry=%s  контейнер=%s"
              % (dn, a.stage, sh.get("entry", "?"), hit))
        members, err = spv_members(sh["spv"])
        if err:
            print("  ", err)
            continue
        bank = dr["cPs"] if a.stage == "ps" else dr["cVs"]
        nz = sum(1 for v in bank if v != 0.0)
        print("  ненулевых float во всём банке: %d из %d" % (nz, len(bank)))
        print("  члены банка (регистр = смещение/16):")
        for off in sorted(members):
            reg = off // 16
            vals = bank[off // 4: off // 4 + 4]
            print("    c%-4d %-28s %s" % (reg, members[off],
                                          " ".join("%.5g" % v for v in vals)))


if __name__ == "__main__":
    main()
