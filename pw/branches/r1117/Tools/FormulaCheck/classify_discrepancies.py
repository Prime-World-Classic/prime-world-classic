#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
classify_discrepancies.py — разбор расхождений <sString> <-> <compiledString> из лога
batch_check.py на «артефакты сравнения» и настоящие расхождения контента.

Почему это нужно: batch_check сравнивает нормализованные сигнатуры (код функции +
релокации + 4-байтные слоты констант/строк). Часть расхождений — не расхождения контента
вообще, а следствия того, как собирался образ: хвосты после NUL строковой константы
(соседние байты образа попадают в сравниваемый 4-байтный слот), разная раскладка образа
при раздельной и пачечной компиляции (смещения релокации уезжают, код тот же).

Классы:
  PADDING   - CONST_DIFF: в каждом отличающемся слоте байты совпадают вплоть до NUL
              строки включительно, различаются только байты после NUL;
  LAYOUT    - CODE_DIFF: шестнадцатеричный код fresh и stored идентичны, различаются
              только списки релокаций (разная раскладка образа);
  REAL      - всё остальное: скомпилированный блоб делает НЕ то, что исходник. Именно
              эти формулы на x64 (FormulaVM исполняет sString) будут вести себя иначе,
              чем на x86 (исполняется блоб).

Запуск:
  python3 Tools/FormulaCheck/classify_discrepancies.py /tmp/formula_full_2026-10-11.log
"""
import argparse
import re
import sys


def parse_log(path):
    items = []
    cur = None
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^(CONST|CODE|NOSYM|CERR)\s+(\S+)\s*$", ln.strip())
        if m:
            if cur:
                items.append(cur)
            cur = {"kind": m.group(1), "file": m.group(2), "fields": {}}
            continue
        if cur is None:
            continue
        m = re.match(r"^\s+(src|fresh|stored|fresh_rel|stored_rel|fresh\(len=\d+\)|stored\(len=\d+\))\)?=?(.*)$", ln.rstrip())
        if m:
            key = m.group(1).split("(")[0]
            cur["fields"].setdefault(key, m.group(2).strip())
        elif ln.strip() and not ln.strip().startswith("пачка") and not ln.strip().startswith("итог"):
            if cur["fields"]:
                items.append(cur)
                cur = None
    if cur:
        items.append(cur)
    return items


def slots(text):
    """['12:b4=b"CD\\x00"', ...] -> {offset: raw bytes} (через literal_eval: лог печатает
    repr внутри repr, руками экранирования не разбирать)"""
    import ast
    out = {}
    try:
        items = ast.literal_eval(text)
    except Exception:
        return out
    for it in items:
        m = re.match(r"^(\d+):b4=(b'.*'|b\".*\")$", it)
        if not m:
            continue
        try:
            out[int(m.group(1))] = ast.literal_eval(m.group(2))
        except Exception:
            pass
    return out


def is_padding(fresh, stored):
    """все отличия - только байты после NUL строковой константы"""
    a, b = slots(fresh), slots(stored)
    if set(a) != set(b) or not a:
        return False
    for off in a:
        x, y = a[off], b[off]
        if x == y:
            continue
        nx = x.find(b"\x00")
        ny = y.find(b"\x00")
        if nx < 2 or ny < 2 or nx != ny:
            return False
        if x[: nx + 1] != y[: ny + 1]:
            return False
        # до NUL должна быть настоящая строка (печатаемые байты): иначе это числовая
        # константа, и отличие после "NUL" = отличие значения (пример: float 5.0 vs 10.0)
        if any(c < 0x20 or c > 0x7E for c in x[:nx]):
            return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    all_items = parse_log(args.log)
    seen, items = set(), []
    for it in all_items:
        k = (it["kind"], it["file"], it["fields"].get("src", ""))
        if k in seen:
            continue
        seen.add(k)
        items.append(it)
    stats = {}
    for it in items:
        f, s = it["fields"].get("fresh", ""), it["fields"].get("stored", "")
        if it["kind"] in ("NOSYM", "CERR"):
            cls = "TOOL_LIMIT"
        elif it["kind"] == "CONST" and is_padding(f, s):
            cls = "PADDING"
        elif it["kind"] == "CODE":
            code_f = it["fields"].get("fresh", "")
            code_s = it["fields"].get("stored", "")
            cls = "LAYOUT" if code_f and code_f == code_s else "REAL"
        else:
            cls = "REAL"
        stats[cls] = stats.get(cls, 0) + 1
        if cls == "REAL" or args.verbose:
            print("%-8s %-6s %s" % (cls, it["kind"], it["file"]))
            print("         src=%s" % it["fields"].get("src", "")[:150])
            if cls != "REAL":
                print("         fresh =%s" % f[:150])
                print("         stored=%s" % s[:150])
    print("\nитого:", stats, "всего расхождений:", len(items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
