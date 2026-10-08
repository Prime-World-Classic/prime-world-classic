#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
batch_check.py — ground-truth сверка <sString> ↔ <compiledString> пакетной
перекомпиляцией исходника формулы ТЕМ компилятором, которым контент собирался
(pw/branches/r1117/Tools/CCompiler/cl.exe = MSVC 15.00.21022.08 x86, под Wine).

Почему пакетно: формул в контенте ~172k, вызов Wine-cl на каждую — часы. В одном
.cpp собирается пачка функций fml_<i> (сигнатура как у abilityFunctor), код каждой
берётся из .text по символам.

Сравнение не побайтовое по блобу (порядок секций/релокаций в одно-TU и
много-TU сборке различается), а по нормализованной сигнатуре функции:
  * байты машинного кода функции, в слотах DIR32/REL32-релокаций — нули
    (смещения vtable-вызовов остаются в байтах: они и ловят рассинхрон);
  * список (смещение, содержимое цели релокации) — фактические использованные
    константы и строки.
Если <sString> правился руками без пересборки — расходится и то, и другое.

Пайплайн: индекс формул -> пачки .cpp -> cl -> разбор .obj -> сравнение.
Ошибки компиляции пачки локализуется бисекцией (сбойная формула не должна
ронять всю пачку).

Запуск:
  python3 Tools/FormulaCheck/batch_check.py <Data-корень> [--dir X]
        [--stride N] [--limit N] [--batch 120] [--work DIR] [--keep] [-v]
"""
import argparse
import base64
import collections
import os
import re
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
R1117 = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import verify_formulas as VF          # noqa: E402
import recompile_check as RC          # noqa: E402  (Coff, gen_cpp, signatures)

CL, INC1, INC2, WINEPREFIX = RC.CL, RC.INC1, RC.INC2, RC.WINEPREFIX


def gen_batch_cpp(items):
    """items: [(tag, sString, returnType)] -> (текст .cpp, {tag: стартовая строка}).
    Стартовые строки нужны, чтобы по сообщению cl «b.cpp(NNN) : error» сразу
    определить, какие формулы не компилируются: перекомпиляция по одной на
    пачку из 100 — это часы под Wine."""
    out = ['#include <math.h>\n#include "FormulaPars.h"\n'
           # В современном FormulaPars.h DSL-функция round() переименована в
           # ni_round() (современный CRT не даёт переопределить round). Старые
           # формулы в контенте вызывают round( — для сверки возвращаем имя
           # макросом ПОСЛЕ include, чтобы не трогать игровой код.
           '#define round(a) ni_round(a)\n    \n']
    start_line = {}
    for tag, s, rt in items:
        body = VF.convert_formula(s)
        start_line[tag] = sum(x.count("\n") for x in out) + 1
        out.append("%s fml_%s(IUnitFormulaPars const *pFirst, IUnitFormulaPars const *pSecond,"
                   " IMiscFormulaPars const *pMisc)\n{\n  return %s;\n}\n\n" % (rt, tag, body))
    return "".join(out), start_line


ERR_LINE_RE = re.compile(r"b\.cpp\((\d+)\)")


def blame(log, start_line):
    """лог cl -> множество tag'ов, в чьих строках ошибки"""
    lines = sorted({int(m.group(1)) for m in ERR_LINE_RE.finditer(log)})
    if not lines:
        return set()
    tags = set()
    for ln in lines:
        cand = [t for t, sl in start_line.items() if sl <= ln]
        if cand:
            tags.add(max(cand, key=lambda t: start_line[t]))
    return tags


def compile_batch(cpp_path, out_dir, timeout=1800):
    env = dict(os.environ, WINEPREFIX=WINEPREFIX, WINEDEBUG="-all",
               WINEDLLOVERRIDES="mscoree,mshtml=")
    # cl запускается из каталога пачки: Wine не понимает абсолютные Linux-пути
    cmd = ["wine", CL, '-I"%s"' % INC1, '-I"%s"' % INC2, "-c", os.path.basename(cpp_path)]
    try:
        r = subprocess.run(cmd, cwd=out_dir, env=env, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "cl timeout"
    log = (r.stdout + r.stderr).decode("cp1251", "replace")
    if r.returncode != 0:
        return None, log
    return cpp_path[:-4] + ".obj", log


def fresh_signatures(obj_path):
    """{tag: (code_bytes, relocs)} по всем fml_<tag> в .obj"""
    c = RC.Coff(obj_path)
    code_sec = next((s for s in c.sections
                     if (s["flags"] & RC.IMAGE_SCN_CNT_CODE)
                     and not s["name"].startswith(".debug") and s["name"] != ".drectve"), None)
    if code_sec is None:
        return {}
    raw = code_sec["raw"]
    funcs = []
    for s in c.symbols:
        m = re.match(r"\?fml_(\w+)@@", s["name"])
        if m and 0 < s["sec"] <= c.nsec and c.sections[s["sec"] - 1] is code_sec:
            funcs.append((int(m.group(1)), s["value"], s["name"]))
    funcs.sort(key=lambda x: x[1])
    out = {}
    for i, (tag, off, name) in enumerate(funcs):
        end = funcs[i + 1][1] if i + 1 < len(funcs) else len(raw)
        sig = bytearray(raw[off:end])
        relocs = []
        for r in code_sec["rels"]:
            if not (off <= r["offs"] < end):
                continue
            buf, tgt = c.sym_target(r["sym"])
            if r["type"] == RC.IMAGE_REL_I386_DIR32:
                relocs.append((r["offs"] - off, RC.target_content(buf, tgt)
                               if buf is not None else ("ext", tgt)))
            elif r["type"] == RC.IMAGE_REL_I386_REL32:
                relocs.append((r["offs"] - off, ("extrel", tgt)))
            else:
                relocs.append((r["offs"] - off, ("reloctype%d" % r["type"], None)))
            for k in range(4):
                if r["offs"] - off + k < len(sig):
                    sig[r["offs"] - off + k] = 0
        out[tag] = (bytes(sig), sorted(relocs))
    return out


def collect(root, subdir, stride, limit):
    items = []
    n = 0
    for path, s, c, rt in VF.iter_formulas(root, subdir):
        if not c or not s.strip():
            continue
        n += 1
        if n % stride:
            continue
        items.append((path, s, c, rt))
        if limit and len(items) >= limit:
            break
    return items


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--dir")
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--limit", type=int, default=240)
    ap.add_argument("--batch", type=int, default=120)
    ap.add_argument("--work", default="/tmp/fbatch")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    items = collect(args.root, args.dir, args.stride, args.limit)
    print("формулы к проверке:", len(items), flush=True)
    os.makedirs(args.work, exist_ok=True)
    stats = collections.Counter()
    bad = []

    for bi in range(0, len(items), args.batch):
        chunk = items[bi:bi + args.batch]
        tag_of = {}
        for i, it in enumerate(chunk):
            tag_of[bi + i] = it
        cpp_items = [(bi + i, it[1], it[3]) for i, it in enumerate(chunk)]
        d = os.path.join(args.work, "b%d" % (bi // args.batch))
        os.makedirs(d, exist_ok=True)
        cpp = os.path.join(d, "b.cpp")
        pending = list(cpp_items)
        obj = None
        for _attempt in range(12):
            text, start_line = gen_batch_cpp(pending)
            open(cpp, "w").write(text)
            obj, log = compile_batch(cpp, d)
            if obj is not None:
                break
            guilty = blame(log, start_line)
            if not guilty:
                errs = [x.strip()[:150] for x in log.splitlines() if "error" in x.lower()]
                print("  пачка %d: ошибка компиляции без привязки к строке:" % (bi // args.batch), flush=True)
                for e in errs[:6]:
                    print("      ", e)
                break
            for t in guilty:
                stats["compile_error"] += 1
                lo = start_line[t]
                hi = min([sl for sl in start_line.values() if sl > lo] or [10 ** 9])
                msg = [x.strip()[:150] for x in log.splitlines()
                       if "error" in x.lower()
                       and any("(N)" in x.replace("(%d)" % N, "(N)") for N in range(lo, hi))]
                bad.append(("COMPILE", tag_of[t][0], tag_of[t][1][:70], (msg or ["?"])[0][:160]))
            pending = [x for x in pending if x[0] not in guilty]
            if not pending:
                break
        if obj is None:
            stats["batch_failed"] += 1
            continue
        sigs = fresh_signatures(obj)
        stats["batch_ok"] += 1
        for tag in tag_of:
            if tag not in sigs:
                stats["no_symbol"] += 1
                bad.append(("NOSYM", tag_of[tag][0], tag_of[tag][1][:70], ""))
                continue
            try:
                check_one(tag, tag_of[tag], sigs[tag], stats, bad)
            except Exception as e:          # одна кривая формула не роняет прогон
                stats["check_error"] += 1
                bad.append(("CHECK", tag_of[tag][0], tag_of[tag][1][:70], repr(e)[:160]))
        if not args.keep:
            shutil.rmtree(d, ignore_errors=True)
        print("  пачка %d: %d формул, всего %s" % (bi // args.batch, len(chunk), dict(stats)), flush=True)

    print("\nитог:", dict(stats))
    print("--- проблемы (%d) ---" % len(bad))
    for kind, path, s, info in bad[:80]:
        print("%-8s %s\n         src=%s\n         %s" % (kind, path, s, info))


RET = b"\x5d\xc3"          # pop ebp; ret — общая точка выхода у /Od-функций


def normalize_local_calls(sig, relocs):
    """Занулить операнды локальных call/jmp (E8/E9 rel32) и отметить их маркером.

    В одно-TU сборке контента вызовы хелперов уже развёрнуты в относительные
    адреса, а в пакетной сборке это ещё релокации на символы — абсолютные
    расстояния при этом разные. Значит сравнивать надо не их, а сами точки
    вызова; чем вызывается — проверяется совпадением остального кода.
    """
    sig = bytearray(sig)
    # записи релокаций, попадающие в операнд локального call/jmp, после зануления
    # бессмысленны (в stored-блобе они уже развёрнуты в адрес) — убираем, иначе
    # fresh/stored расходятся только из-за них
    call_slots = set()
    i = 0
    while i < len(sig) - 4:
        if sig[i] in (0xE8, 0xE9):
            call_slots.update(range(i + 1, i + 5))
            i += 5
            continue
        i += 1
    out = [x for x in relocs if x[0] not in call_slots]
    i = 0
    while i < len(sig) - 4:
        if sig[i] in (0xE8, 0xE9):
            out.append(("relcall", i, sig[i]))
            for k in range(1, 5):
                sig[i + k] = 0
            i += 5
            continue
        i += 1
    return bytes(sig), sorted(out, key=lambda x: str(x))


def cut(code, first):
    """отсечь хвост: у свежей сборки это padding 0xCC и данные следующих
    функций, у stored — секции констант после функции."""
    code = code.rstrip(b"\xcc")
    i = code.rfind(RET) if not first else code.find(RET)
    return code[:i + 2] if i >= 0 else code


def fmt_rel(rel):
    out = []
    for item in rel:
        if item[0] == "relcall":          # (marker, offset, opcode)
            out.append("%d:%s" % (item[1], "call" if item[2] == 0xE8 else "jmp"))
            continue
        off, val = item[0], item[1]
        kind = val[0]
        if kind == "f4":
            out.append("%d:%s" % (off, RC.content_hint(val)))
        else:
            out.append("%d:%s=%s" % (off, kind, val[1]))
    return out


def check_one(tag, item, fresh, stats, bad):
    path, s, c, rt = item
    try:
        stored = RC.stored_signature(c)
    except Exception as e:
        stats["stored_decode_error"] += 1
        bad.append(("DECODE", path, s[:70], str(e)))
        return
    f_code = cut(fresh[0], False)
    s_code = cut(stored[0], True)
    f_code, f_rel = normalize_local_calls(f_code, [x for x in fresh[1] if x[0] < len(f_code)])
    s_code, s_rel = normalize_local_calls(s_code, [x for x in stored[1] if x[0] < len(s_code)])
    if f_code == s_code and f_rel == s_rel:
        stats["MATCH"] += 1
    elif f_code == s_code:
        # код совпал, а константы/строки разные — типичный след ручной правки
        # <sString> без пересборки (или наоборот)
        stats["CONST_DIFF"] += 1
        bad.append(("CONST", path, s[:70], "fresh=%s\n         stored=%s" % (fmt_rel(f_rel)[:8], fmt_rel(s_rel)[:8])))
    else:
        stats["CODE_DIFF"] += 1
        bad.append(("CODE", path, s[:70],
                    "fresh(len=%d)=%s\n         stored(len=%d)=%s\n         fresh_rel=%s\n         stored_rel=%s" %
                    (len(f_code), f_code.hex(" ")[:180], len(s_code), s_code.hex(" ")[:180],
                     f_rel[:4], s_rel[:4])))


if __name__ == "__main__":
    main()
