#!/usr/bin/env python3
"""Сбор ЖИВЫХ комбинаций (материал, индекс пинов) из логов клиента.

Индекс пары в `.shd` = сумма pinValue * pinMultiplier по пинам материала
(`Src/Render/MaterialSpec.h:213`), пространство индексов плотное и огромное
(`UIFlashMaterial` — 4096 комбинаций, `BasicMaterial` — 49966). Для SPIR-V-цели
(шаг 11, PLAN_NVRHI §3.z п.3) компилировать всё пространство нельзя — нужен
список реально используемых индексов.

Источник — dev-var клиента `dump_shader_index` (печатает одну строку на пару
при первом обращении; см. `Src/Render/multishader.cpp`):

    setvar dump_shader_index 1        # в Game/Profiles/autoexec.cfg

Строка лога:

    ShaderIndex: Shaders/UberShaders/UIFlashMaterial #1153 (of 4096)

Использование:
    tools/shader_build/collect_live_indices.py <лог>... --out live.json
    tools/shader_build/collect_live_indices.py --merge live.json <лог>...

Логи берутся со стенда: `C:\\Users\\rekojr\\Documents\\My Games\\Prime World
Classic\\Session\\Logs\\<дата>` (читать по SSH через `findstr /c:"ShaderIndex"`).
Сценариев нужно несколько (бой, лобби, разные карты/режимы) — промах по
индексу на Vulkan-пути означает компиляцию в рантайме либо `default_shader`.
"""

import argparse
import json
import os
import re
import sys

LINE_RE = re.compile(
    r"ShaderIndex:\s+Shaders/UberShaders/(?P<mat>\w+)\s+#(?P<idx>\d+)\s+\(of\s+(?P<total>\d+)\)")


def load(path):
    if not os.path.exists(path):
        return {"sources": [], "materials": {}}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def add_log(state, path):
    text = sys.stdin.read() if path == "-" else open(path, encoding="utf-8",
                                                     errors="replace").read()
    hits = 0
    for m in LINE_RE.finditer(text):
        mat = m.group("mat")
        entry = state["materials"].setdefault(mat, {"total": int(m.group("total")),
                                                    "indices": []})
        idx = int(m.group("idx"))
        if idx not in entry["indices"]:
            entry["indices"].append(idx)
        hits += 1
    if path not in state["sources"]:
        state["sources"].append(path)
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="*", help="файлы логов клиента ('-' = stdin)")
    ap.add_argument("--out", help="куда записать JSON")
    ap.add_argument("--merge", help="существующий JSON, в который дописывать")
    ap.add_argument("--summary", action="store_true", help="только сводка")
    args = ap.parse_args()

    state = load(args.merge) if args.merge else {"sources": [], "materials": {}}
    total_lines = 0
    for log in args.logs:
        total_lines += add_log(state, log)

    pairs = sum(len(v["indices"]) for v in state["materials"].values())
    space = sum(v["total"] for v in state["materials"].values())
    print(f"строк разбора: {total_lines}, материалов: {len(state['materials'])}, "
          f"живых пар: {pairs}, всё пространство индексов: {space}")
    if not args.summary:
        for mat in sorted(state["materials"],
                          key=lambda m: -len(state["materials"][m]["indices"])):
            v = state["materials"][mat]
            ids = sorted(v["indices"])
            shown = ",".join(str(i) for i in ids[:12]) + ("…" if len(ids) > 12 else "")
            print(f"  {mat:28s} {len(ids):3d} из {v['total']:6d}  [{shown}]")
    if args.out:
        state["materials"] = {m: {"total": v["total"], "indices": sorted(v["indices"])}
                              for m, v in sorted(state["materials"].items())}
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(state, f, ensure_ascii=False, indent=1)
        print(f"записано: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
