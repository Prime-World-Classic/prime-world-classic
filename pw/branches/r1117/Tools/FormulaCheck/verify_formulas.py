#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_formulas.py — сверка <sString> (исходник формулы) с <compiledString>
(предкомпилированный x86-код) в контенте (.xdb).

Зачем (PLAN_client_modern.md, этап 3, решение по формулам): на x64 формулы будут
компилироваться из sString, значит sString обязан быть актуальным. Формулы в
контенте правились и руками, поэтому «sString == исходник, из которого собран
compiledString» надо проверить, а не предполагать.

Как работает (без компилятора, покрывает все формулы):
  1. из .xdb берутся тройки (sString, compiledString, returnType);
  2. compiledString (base64) раскладывается по формату DataExecutor::FormulaHeader
     (version, entryPointOffset, relocsNum, extRelocsNum, таблиц релокаций, код);
  3. из кода точки входа вынимается МНОГОМНОЖЕСТВО адресов виртуальных вызовов
     (call [reg+disp] — это аксессоры интерфейсов формул) и константы, на которые
     указывают DIR32-релокации;
  4. из sString тем же набором правил, что и Src/FormulaBuilder/FormulaBuilder.cpp
     (порядок замен важен!), выводится ожидаемый набор аксессоров;
  5. имя аксессора -> индекс в vtable по порядку объявления в
     Data/GameLogic/FormulaPars.h -> ожидаемый disp. Сравниваем.

Точность: шаг 3 использует эвристику (код функции до первого `pop ebp; ret`),
поэтому вывод делится на MISMATCH (набор аксессоров не совпал — почти наверняка
ручная правка) и SUSPECT (лишние disp — возможен ложный срабатывание эвристики,
нужен прогон через реальный компилятор).

Запуск:
  python3 Tools/FormulaCheck/verify_formulas.py <Data-корень> [--limit N]
                                                [--dir подпуть] [--verbose]
"""
import argparse
import base64
import binascii
import collections
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

FORMULA_VERSION = 21          # Data/GameLogic/FormulaPars.h: #define FORMULA_VERSION 21
HEADER_FMT = "<BIHH"          # pack(1): version, entryPointOffset, relocsNum, extRelocsNum
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 9

# ---------------------------------------------------------------------------
# 1. интерфейс -> порядок vtable-слотов
# ---------------------------------------------------------------------------

def load_vtable(formulapars_h):
    """{struct_name: {method_name: slot_index}} по порядку объявления virtual."""
    txt = open(formulapars_h, "rb").read().decode("cp1251", "replace")
    out = {}
    for m in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\n\};", txt, re.S):
        name, body = m.group(1), m.group(2)
        slots = {}
        idx = 0
        for vm in re.finditer(r"virtual\s+[^;=()]+?\b(\w+)\s*\(", body):
            meth = vm.group(1)
            if meth not in slots:          # перегрузки делят смысл имени
                slots[meth] = idx
                idx += 1
        if slots:
            out[name] = slots
    return out

# ---------------------------------------------------------------------------
# 2. правила FormulaBuilder (порт; порядок замен = порядок в PrepareCFile)
# ---------------------------------------------------------------------------
# В C++ использованы lookbehind вида (?:(?<=[^A-Za-z_])|(?<=^)); в Python
# смешанная ширина lookbehind запрещена, но все эти правила стоят перед
# буквой/цифрой, поэтому эквивалентно \b. Правила с (?:(?<=\->)|(?<=\.))
# оставлены чередованием lookbehind'ов (каждый фиксированной ширины).
PREV = r"\b"
DOTARROW = r"(?:(?<=\->)|(?<=\.))"

RULES = [
    (PREV + r"sFlag\(([A-Z0-9]\w*)\)",                                   r"pFirst->GetFlag(\1)"),
    (PREV + r"rFlag\(([A-Z0-9]\w*)\)",                                   r"pSecond->GetFlag(\1)"),
    (PREV + r"sIs([A-Z]\w*)",                                            r"pFirst->Is\1()"),
    (PREV + r"rIs([A-Z]\w*)",                                            r"pSecond->Is\1()"),
    (PREV + r"mIs([A-Z]\w*)",                                            r"pMisc->Is\1()"),
    (PREV + r"s([A-Z]\w*)",                                              r"pFirst->Get\1()"),
    (PREV + r"r([A-Z]\w*)",                                              r"pSecond->Get\1()"),
    (PREV + r"m([A-Z]\w*)",                                              r"pMisc->Get\1()"),
    (PREV + r"r\(",                                                      r"SwitchByAbilityRank(pMisc->GetRank(),"),
    (PREV + r"c([A-Z]\w*)",                                              r'pMisc->GetConstant("\1", pFirst, pSecond)'),
    (PREV + r"ut_hero\(",                                                r"SwitchByBool(pSecond->UnitCheck(UNITCHECKID_ISHERO),"),
    (PREV + r"ut_bldg\(",                                                r"SwitchByBool(pSecond->UnitCheck(UNITCHECKID_ISBUILDING),"),
    (PREV + r"s\(",                                                      r"SwitchByBool("),
    (PREV + r"roll\(([A-Za-z_0-9->\(\)]+)",                              r"SwitchByBool(pMisc->Roll(\1)"),
    # ВНИМАНИЕ: в Src/FormulaBuilder эти правила жадные (`sender\.(.*)`) — при
    # двух вхождениях в одной формуле второе не конвертируется и C++ не
    # компилируется. Контент-пайплайн так не делает (формулы вида
    # `receiver.BaseStrength >= receiver.BaseIntellect` в контенте откомпилированы),
    # поэтому здесь захват только имени; цепочка `->`/`.` дальше дотягивают
    # правила ниже. Расхождение с Src/FormulaBuilder зафиксировано намеренно.
    (PREV + r"sender\.([A-Za-z_0-9]+)",                                  r"pFirst->\1"),
    (PREV + r"receiver\.([A-Za-z_0-9]+)",                                r"pSecond->\1"),
    (PREV + r"appl\.([A-Za-z_0-9]+)",                                    r"pMisc->\1"),
    (DOTARROW + r"appl([A-Z][A-Za-z_0-9]*)\(([A-Za-z_0-9]+)\)\.(?:(?=[A-Za-z_0-9]+)|(?=$))",
                                                          r'FindApplicator("\1", pMisc, \2)->'),
    (DOTARROW + r"appl([A-Z][A-Za-z_0-9]*)\(([A-Za-z_0-9]+)\)(?:(?=[\s\+\-\?\\\/\*><=\),\.])|(?=$))",
                                                          r'FindApplicator("\1", pMisc, \2)'),
    (DOTARROW + r"v([A-Z][A-Za-z_0-9]*)(?:(?=[\s\+\-\?\\\/\*><=\),])|(?=$))", r'GetVariable("\1")'),
    (DOTARROW + r"c([A-Z][A-Za-z_0-9]*)(?:(?=[\s\+\-\?\\\/\*><=\),])|(?=$))",
                                                          r'GetConstant("\1", pFirst, pSecond)'),
    (DOTARROW + r"o([A-Z][A-Za-z_0-9]*)\.",                              r'GetObject("\1")->'),
    (DOTARROW + r"([A-Z][A-Za-z_0-9]*)(?:(?=[\s\+\-\?\\\/\*><=\),])|(?=$))", r"Get\1()"),
    (r"(?<=\->)([a-z]+?)\.",                                             r"GetObjectName()"),
]
RULES_C = [(re.compile(p), r) for p, r in RULES]

def convert_formula(s, alt_second=False):
    """sString -> C++-выражение ровно в том порядке, как в FormulaBuilder."""
    out = s
    for rx, rep in RULES_C:
        if alt_second and ("pSecond" in rep or "pSecond->UnitCheck" in rep):
            continue
        out = rx.sub(rep, out)
    return out

CALL_RE = re.compile(r"\b(pFirst|pSecond|pMisc)->(\w+)\s*\(")

def expected_accessors(s, alt_second=False):
    """мультимножество (интерфейс, имя метода) из исходника формулы"""
    return collections.Counter(CALL_RE.findall(convert_formula(s, alt_second)))

# ---------------------------------------------------------------------------
# 3. разбор compiledString
# ---------------------------------------------------------------------------

class BlobError(Exception):
    pass

def decode_blob(b64):
    raw = base64.b64decode(b64.strip())
    if len(raw) < HEADER_SIZE:
        raise BlobError("short blob")
    version, entry, nrelocs, nextrelocs = struct.unpack_from(HEADER_FMT, raw, 0)
    hsize = HEADER_SIZE + 4 * (nrelocs + nextrelocs)
    if hsize > len(raw):
        raise BlobError("header bigger than blob")
    relocs = list(struct.unpack_from("<%dI" % nrelocs, raw, HEADER_SIZE))
    extrelocs = list(struct.unpack_from("<%dI" % nextrelocs, raw, HEADER_SIZE + 4 * nrelocs))
    code = raw[hsize:]
    return dict(version=version, entry=entry, relocs=relocs, extrelocs=extrelocs,
                code=code, raw=raw, hsize=hsize)

# код точки входа: до первого `5D C3` (pop ebp; ret) — эвристика для /Od-кода
RET_SEQ = b"\x5d\xc3"

def entry_code(blob):
    code = blob["code"]
    e = blob["entry"]
    if e >= len(code):
        raise BlobError("entry out of range")
    body = code[e:]
    i = body.find(RET_SEQ)
    if i < 0:
        return body, False
    return body[:i + 2], True

# call [reg+disp32]: FF /2, mod=10 -> FF 50..5F
CALL_IND32 = re.compile(rb"\xff[\x50-\x5f](.{4})", re.S)
# call [reg+disp8]: FF /2, mod=01 -> FF 51..5F с одним байтом disp
CALL_IND8 = re.compile(rb"\xff[\x51-\x5f].", re.S)

def blob_calls(blob):
    """мультимножество disp у косвенных вызовов (vtable-смещения аксессоров)"""
    body, _ = entry_code(blob)
    disp = collections.Counter()
    for m in CALL_IND32.finditer(body):
        d = struct.unpack("<I", m.group(1))[0]
        if 0 < d < 4 * 512:
            disp[d] += 1
    return disp

def blob_constants(blob):
    """float-константы, на которые указывают DIR32-релокации внутри блоба"""
    raw, code = blob["raw"], blob["code"]
    vals = []
    for off in blob["relocs"]:
        if off + 4 > len(raw):
            continue
        target = struct.unpack_from("<I", raw, off)[0]
        if 0 <= target <= len(code) - 4:
            vals.append(struct.unpack_from("<f", code, target)[0])
    return vals

# ---------------------------------------------------------------------------
# 4. обход контента
# ---------------------------------------------------------------------------

def iter_formulas(root, subdir=None):
    base = os.path.join(root, subdir) if subdir else root
    for dirpath, _dirs, files in os.walk(base):
        for fn in sorted(files):
            if not fn.endswith(".xdb"):
                continue
            path = os.path.join(dirpath, fn)
            try:
                tree = ET.parse(path)
            except ET.ParseError:
                continue
            root_el = tree.getroot()
            for el in root_el.iter():
                s = el.find("sString")
                c = el.find("compiledString")
                if s is None or c is None:
                    continue
                rt = el.find("returnType")
                yield (path, (s.text or "").strip(), (c.text or "").strip(),
                       (rt.text or "float").strip() if rt is not None else "float")

# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--dir", default=None, help="подпуть внутри Data")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--max-report", type=int, default=40)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    r1117 = os.path.dirname(os.path.dirname(here))
    vt = load_vtable(os.path.join(r1117, "Data", "GameLogic", "FormulaPars.h"))
    iface = {"pFirst": "IUnitFormulaPars", "pSecond": "IUnitFormulaPars", "pMisc": "IMiscFormulaPars"}
    disp2name = {}
    for var, sname in iface.items():
        for meth, idx in vt.get(sname, {}).items():
            disp2name.setdefault(4 * idx, meth)   # MSVC: i-й virtual лежит у [this+4*i]

    stats = collections.Counter()
    reports = []
    n = 0
    for path, s, c, rt in iter_formulas(args.root, args.dir):
        n += 1
        if args.limit and n > args.limit:
            break
        if not c:
            stats["no_compiled"] += 1
            continue
        try:
            blob = decode_blob(c)
        except (BlobError, binascii.Error, struct.error) as e:
            stats["decode_error"] += 1
            if len(reports) < args.max_report:
                reports.append(("DECODE", path, s[:60], str(e)))
            continue
        if blob["version"] != FORMULA_VERSION:
            stats["version_mismatch"] += 1
            if len(reports) < args.max_report:
                reports.append(("VERSION", path, s[:60], "version=%d" % blob["version"]))
            continue
        exp = expected_accessors(s)
        got = blob_calls(blob)
        # disp -> имя; неизвестные disp оставляем числами
        exp_disp = collections.Counter()
        unknown_names = []
        for (var, meth), cnt in exp.items():
            idx = vt.get(iface[var], {}).get(meth)
            if idx is None:
                unknown_names.append("%s->%s" % (var, meth))
                continue
            exp_disp[4 * idx] += cnt
        stats["total"] += 1
        if not got and not exp_disp:
            stats["const_only_ok"] += 1
            continue
        if exp_disp == got:
            stats["ok"] += 1
            continue
        extra = got - exp_disp
        missing = exp_disp - got
        kind = "MISMATCH"
        if missing and not extra:
            kind = "MISMATCH"
        elif extra and not missing:
            kind = "SUSPECT"
        stats[kind.lower()] += 1
        if len(reports) < args.max_report or args.verbose:
            reports.append((kind, path, s[:70],
                            "missing=%s extra=%s unknown=%s" % (
                                {disp2name.get(d, hex(d)): c for d, c in missing.items()},
                                {disp2name.get(d, hex(d)): c for d, c in extra.items()},
                                unknown_names[:4])))
    print("просканировано формул:", n)
    for k, v in sorted(stats.items()):
        print("  %-16s %d" % (k, v))
    print("\n--- отчёты (%d показано) ---" % len(reports))
    for kind, path, s, info in reports:
        print("%-9s %s\n          src=%s\n          %s" % (kind, path, s, info))

if __name__ == "__main__":
    main()
