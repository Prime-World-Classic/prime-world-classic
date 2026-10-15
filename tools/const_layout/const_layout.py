#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Раскладка «виртуальных регистров» констант шейдеров (c0..c255) из GlobalMasks.h.

Зачем (docs/PLAN_NVRHI.md §2.7, шаг 7 очереди). На Vulkan константы шейдера —
один uniform-буфер на стадию, и прежде чем писать NvrhiBackend, надо знать:
  1) какие регистры вообще названы (по ним генерируется layout cbuffer'а);
  2) какие из них перекрываются (один и тот же cN у разных шейдеров значит
     разное — для плоского массива float4 это алиас, и он законен, но его надо
     видеть: именно он ломает идею «под-буфера на материал»);
  3) чем заполнена дыра между названными и реально живущими регистрами — это
     даёт сверка с логом клиента (строка `ConstStore: VS c0..c234 ... | PS ...`,
     §2.9/§2.12).

Запуск:
  tools/const_layout/const_layout.py [путь к GlobalMasks.h] [--log лог_клиента]

Выход — текстовый отчёт: карта регистров по стадиям, пересечения, покрытие,
и (при --log) «живые, но не названные» диапазоны.
"""

import re
import sys

DEFAULT_HEADER = "pw/branches/r1117/Src/Render/GlobalMasks.h"

# Заголовки секций в GlobalMasks.h. Стадия определяет, в каком пространстве
# счёта живут последующие #define cN (VS-пространство и PS-пространство —
# независимые 0..255).
STAGE_MARKS = [
    (re.compile(r"^//\s*Vertex Shader", re.M), "VS"),
    (re.compile(r"^//\s*Pixel Shader", re.M), "PS"),
]

DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+([cs])(\d+)\b(.*)$", re.M)
# Количество регистров принято писать в хвостовом комментарии: «// 4 regs».
REGS_RE = re.compile(r"(\d+)\s*regs?", re.I)
# Строка замера из лога клиента (Render/conststore.cpp, ConstStore::StatsLine).
STATS_RE = re.compile(
    r"ConstStore:\s*VS\s*c(\d+)\.\.c(\d+)\s*\((\d+)\s*reg.*?\)\s*\|\s*"
    r"PS\s*c(\d+)\.\.c(\d+)\s*\((\d+)\s*reg", re.S)


def parse(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    # Секции: позиция заголовка стадии -> стадия.
    marks = []
    for rx, stage in STAGE_MARKS:
        for m in rx.finditer(text):
            marks.append((m.start(), stage))
    marks.sort()

    def stage_at(pos):
        cur = "?"
        for p, s in marks:
            if p <= pos:
                cur = s
            else:
                break
        return cur

    out = {"VS": [], "PS": [], "?": []}
    for m in DEFINE_RE.finditer(text):
        name, kind, num, tail = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        if kind != "c":            # sN — сэмплеры, их сюда не берём
            continue
        regs = REGS_RE.search(tail)
        out[stage_at(m.start())].append(
            {"name": name, "reg": num, "regs": int(regs.group(1)) if regs else None})
    return out


def fill_spans(entries, limit=256):
    """Явный размер есть не у всех; для остальных — расстояние до следующего
    названного регистра (в пределах стадии), минимум 1. Такие размеры помечаются
    как выведенные: на них нельзя строить вывод о покрытии (последний регистр
    секции «дотягивается» до конца пространства)."""
    entries = sorted(entries, key=lambda e: e["reg"])
    for i, e in enumerate(entries):
        if e["regs"]:
            e["inferred"] = False
            continue
        nxt = None
        for j in range(i + 1, len(entries)):
            if entries[j]["reg"] > e["reg"]:
                nxt = entries[j]["reg"]
                break
        e["regs"] = max(1, (nxt if nxt is not None else limit) - e["reg"])
        e["inferred"] = True
    return entries


def report_stage(stage, entries, live=None):
    entries = fill_spans(entries)
    print(f"\n=== {stage}: {len(entries)} названных регистров ===")
    for e in entries:
        span = f"c{e['reg']}" + (f"..c{e['reg'] + e['regs'] - 1}" if e["regs"] > 1 else "")
        mark = "~" if e["inferred"] else " "
        print(f" {mark} {span:<16} {e['name']}")
    print("   (`~` — размер выведен по следующему имени, в отчёте о покрытии не участвует)")

    # Пересечения: один диапазон накрывает другой или пересекается с ним.
    print(f"  --- пересечения ---")
    found = 0
    for i in range(len(entries)):
        for j in range(i + 1, len(entries)):
            a, b = entries[i], entries[j]
            lo, hi = max(a["reg"], b["reg"]), min(a["reg"] + a["regs"], b["reg"] + b["regs"])
            if hi > lo:
                found += 1
                print(f"    {a['name']} (c{a['reg']}+{a['regs']}) x "
                      f"{b['name']} (c{b['reg']}+{b['regs']}) = c{lo}..c{hi - 1}")
    if not found:
        print("    нет")

    # Покрытие: какие регистры не накрываются ни одним названным диапазоном.
    # Считаем только по явным размерам — выведенные («~») намеренно широкие.
    covered = [False] * 256
    for e in entries:
        if e["inferred"]:
            continue
        for r in range(e["reg"], min(256, e["reg"] + e["regs"])):
            covered[r] = True
    named = sum(covered)
    print(f"  покрытие явными размерами: {named}/256 регистров")
    if live:
        lo, hi = live
        holes = [r for r in range(lo, min(hi + 1, 256)) if not covered[r]]
        print(f"  живёт c{lo}..c{hi}, из них без имени: {len(holes)}"
              + (f" (первый c{holes[0]})" if holes else ""))


def live_from_log(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = None
    for m in STATS_RE.finditer(text):
        pass                      # берём последнее вхождение — конец прогона
    if not m:
        return None
    return {"VS": (int(m.group(1)), int(m.group(2))),
            "PS": (int(m.group(4)), int(m.group(5)))}


def main(argv):
    args = [a for a in argv[1:]]
    log = None
    if "--log" in args:
        i = args.index("--log")
        log = args[i + 1]
        del args[i:i + 2]
    path = args[0] if args else DEFAULT_HEADER
    print(f"GlobalMasks.h: {path}")
    stages = parse(path)
    if stages["?"]:
        print(f"вне секций стадии (не учтено): {[e['name'] for e in stages['?']]}")
    live = live_from_log(log) if log else None
    if live:
        print(f"живой диапазон из лога: VS c{live['VS'][0]}..c{live['VS'][1]}, "
              f"PS c{live['PS'][0]}..c{live['PS'][1]}")
    for stage in ("VS", "PS"):
        report_stage(stage, stages[stage], live[stage] if live else None)


if __name__ == "__main__":
    main(sys.argv)
