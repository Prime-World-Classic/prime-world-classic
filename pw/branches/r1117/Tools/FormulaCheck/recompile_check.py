#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
recompile_check.py — ground-truth сверка <sString> ↔ <compiledString>.

Компилируем sString тем же компилятором, которым контент собирался
(pw/branches/r1117/Tools/CCompiler/cl.exe = MSVC 15.00.21022.08 x86, под Wine),
и сравниваем результат с тем, что лежит в контенте.

Сравнение НЕ побайтовое по всему блобу (порядок секций и раз resolution'ов
могут отличаться), а по нормализованной сигнатуре функции:
  * байты машинного кода функции, в слотах DIR32-релокаций — нули;
  * список (смещение релокации, содержимое цели) — т.е. какие константы/строки
    реально используются, в каких местах кода;
  * смещения косвенных вызовов (vtable disp) остаются в байтах кода.
Если исходник формулы правился руками без пересборки — сигнатуры разойдутся.

Запуск:
  python3 Tools/FormulaCheck/recompile_check.py <Data-корень> [--dir X]
        [--stride N] [--limit N] [--jobs J] [--keep] [--verbose]
"""
import argparse
import base64
import collections
import os
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
R1117 = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import verify_formulas as VF          # noqa: E402

CL = os.path.join(R1117, "Tools", "CCompiler", "cl.exe")
INC1 = os.path.join(R1117, "Data", "GameLogic")
INC2 = os.path.join(R1117, "Tools", "CCompiler")
WINEPREFIX = os.path.expanduser("~/pwbuild/wine32")

# mangled имена точки входа (Src/FormulaBuilder/FormulaBuilder.cpp: _cpEntryPointNames)
ENTRY_NAMES = {
    "float": "?abilityFunctor@@YAMPBUIUnitFormulaPars@@0PBUIMiscFormulaPars@@@Z",
    "int":   "?abilityFunctor@@YAHPBUIUnitFormulaPars@@0PBUIMiscFormulaPars@@@Z",
    "bool":  "?abilityFunctor@@YA_NPBUIUnitFormulaPars@@0PBUIMiscFormulaPars@@@Z",
}
ENTRY_ALT = "?abilityFunctor@@YAMPBUIUnitFormulaPars@@PBUICustomFormulaPars@@PBUIMiscFormulaPars@@@Z"

IMAGE_REL_I386_DIR32 = 6
IMAGE_REL_I386_REL32 = 20
IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x40


# ---------------------------------------------------------------- COFF reader
class Coff:
    def __init__(self, path):
        d = open(path, "rb").read()
        # .obj у MSVC — чистый COFF без DOS-заголовка; .exe/.dll — PE с "MZ"
        if d[:2] == b"MZ":
            coff_off = struct.unpack_from("<I", d, 0x3C)[0]
            assert d[coff_off:coff_off + 4] == b"PE\0\0", "not PE"
            coff_off += 4
        else:
            coff_off = 0
        (self.machine, self.nsec, _ts, self.pSym, self.nSym,
         self.cbOpt, _ch) = struct.unpack_from("<HHIIIHH", d, coff_off)
        off = coff_off + 20 + self.cbOpt
        self.sections = []
        for i in range(self.nsec):
            sh = d[off + 40 * i: off + 40 * (i + 1)]
            name = sh[:8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, rawsize, rawptr, relptr, lnptr, nrel, nl, flags = \
                struct.unpack("<IIIIIIHHI", sh[8:40])
            rels = []
            for k in range(nrel):
                v, sm, t = struct.unpack_from("<IIH", d, relptr + 10 * k)
                rels.append(dict(offs=v, sym=sm, type=t))
            self.sections.append(dict(name=name, raw=d[rawptr:rawptr + rawsize],
                                      rawsize=rawsize, flags=flags, rels=rels))
        self.symbols = []
        p = self.pSym
        strtab = d[p + 18 * self.nSym:]
        i = 0
        while i < self.nSym:
            rec = d[p + 18 * i: p + 18 * i + 18]
            # COFF: если первые 4 байта имени нулевые — имя длинное и лежит в
            # строковой таблице по смещению из байт 4..8
            if struct.unpack_from("<I", rec, 0)[0] == 0:
                zoff = struct.unpack_from("<I", rec, 4)[0]
                end = strtab.index(b"\0", zoff)
                name = strtab[zoff:end].decode("ascii", "replace")
            else:
                name = rec[:8].rstrip(b"\0").decode("ascii", "replace")
            value, sec, typ, sclass, naux = struct.unpack("<iHHBB", rec[8:18])
            self.symbols.append(dict(name=name, value=value, sec=sec, type=typ,
                                     sclass=sclass, idx=i))
            # aux-записи сохраняем слотами: индексы релокаций считаются по всем
            # записям симovol-таблицы, включая aux
            for a in range(naux):
                self.symbols.append(dict(name="<aux>", value=0, sec=0, type=0,
                                         sclass=0, idx=i + a + 1))
            i += naux + 1
        # цель релокации разбирается на месте в sym_target()

    def sym_target(self, sym_idx):
        """(raw bytes секции, смещение) для символа; None — внешний символ."""
        s = self.symbols[sym_idx]
        if 0 < s["sec"] <= self.nsec:
            return self.sections[s["sec"] - 1]["raw"], s["value"]
        return None, s["name"]


def gen_cpp(sstring, ret, alt_second=False):
    body = VF.convert_formula(sstring, alt_second)
    sig = ("float abilityFunctor(IUnitFormulaPars const *pFirst, "
           "ICustomFormulaPars const *pSecond, IMiscFormulaPars const *pMisc)"
           if alt_second else
           "%s abilityFunctor(IUnitFormulaPars const *pFirst, "
           "IUnitFormulaPars const *pSecond, IMiscFormulaPars const *pMisc)" % ret)
    return '#include <math.h>\n#include "FormulaPars.h"\n    \n%s\n{\n  return %s;\n}\n\n' % (sig, body)


def compile_obj(cpp_path, obj_dir):
    env = dict(os.environ, WINEPREFIX=WINEPREFIX, WINEDEBUG="-all",
               WINEDLLOVERRIDES="mscoree,mshtml=")
    cmd = ["wine", CL, '-I"%s"' % INC1, '-I"%s"' % INC2, "-c", os.path.basename(cpp_path),
           "-Fo."]
    r = subprocess.run(cmd, cwd=obj_dir, env=env, capture_output=True, timeout=180)
    out = (r.stdout + r.stderr).decode("cp1251", "replace")
    if r.returncode != 0:
        return None, out
    return cpp_path[:-4] + ".obj", out


def read_cstr(buf, off):
    end = buf.find(b"\0", off)
    return buf[off:end] if end >= 0 else buf[off:]


def target_content(buf, off):
    """Содержимое цели релокации для СРАВНЕНИЯ — всегда 4 байта.

    Раньше цель классифицировалась как строка/число по признаку «читаемые байты
    до NUL». Из-за этого одна и та же константа в свежей (много-TU) и в
    контентной (одно-TU) сборке получала разную метку — соседние байты разные,
    длина «читаемого»run'а разная. Итог: ложные CONST_DIFF. Теперь метка одна,
    человекоразборчивый вид даёт fmt_rel."""
    if buf is None:
        return ("sym", off)
    return ("b4", buf[off:off + 4])


def content_hint(val):
    """человекочитаемое представление цели релокации"""
    b = val[1]
    if isinstance(b, (bytes, bytearray)) and len(b) == 4:
        raw = b
        if all(32 <= c < 127 for c in raw):
            return "str?%s f?%s" % (raw.decode("ascii"), struct.unpack("<f", raw)[0])
        return "f%g" % struct.unpack("<f", raw)[0]
    return repr(b)


def fresh_signature(obj_path):
    c = Coff(obj_path)
    keep = [s for s in c.sections
            if (s["flags"] & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA))
            and not s["name"].startswith(".debug") and s["name"] != ".drectve"]
    # функция = секция кода, содержащая abilityFunctor
    code_sec = next((s for s in keep if s["flags"] & IMAGE_SCN_CNT_CODE), None)
    if code_sec is None:
        return None
    func = code_sec["raw"]
    sig = bytearray(func)
    relocs = []
    for r in code_sec["rels"]:
        if r["type"] == IMAGE_REL_I386_DIR32:
            buf, tgt = c.sym_target(r["sym"])
            if buf is None:
                relocs.append((r["offs"], ("ext", tgt)))
            else:
                relocs.append((r["offs"], target_content(buf, tgt)))
            for k in range(4):
                if r["offs"] + k < len(sig):
                    sig[r["offs"] + k] = 0
        elif r["type"] == IMAGE_REL_I386_REL32:
            _buf, tgt = c.sym_target(r["sym"])
            relocs.append((r["offs"], ("extrel", tgt)))
            for k in range(4):
                if r["offs"] + k < len(sig):
                    sig[r["offs"] + k] = 0
    return bytes(sig), sorted(relocs)


def stored_signature(b64):
    b = VF.decode_blob(b64)
    code = b["code"]
    func = code[b["entry"]:]
    sig = bytearray(func)
    relocs = []
    for off in b["relocs"]:
        if off < b["entry"]:
            continue
        target = struct.unpack_from("<I", code, off)[0]
        relocs.append((off - b["entry"], target_content(code, target)
                       if 0 <= target < len(code) else ("oob", target)))
        for k in range(4):
            if off - b["entry"] + k < len(sig):
                sig[off - b["entry"] + k] = 0
    for off in b["extrelocs"]:
        if off >= b["entry"]:
            relocs.append((off - b["entry"], ("extrel", None)))
            for k in range(4):
                if off - b["entry"] + k < len(sig):
                    sig[off - b["entry"] + k] = 0
    return bytes(sig), sorted(relocs), b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--dir")
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--limit", type=int, default=50)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--work", default="/tmp/fchk")
    args = ap.parse_args()

    os.makedirs(args.work, exist_ok=True)
    stats = collections.Counter()
    bad = []
    n = 0
    for path, s, c, rt in VF.iter_formulas(args.root, args.dir):
        if not c or not s.strip():
            continue
        n += 1
        if n % args.stride:
            continue
        if stats["checked"] >= args.limit:
            break
        d = os.path.join(args.work, "f%d" % n)
        os.makedirs(d, exist_ok=True)
        cpp = os.path.join(d, "t.cpp")
        open(cpp, "w").write(gen_cpp(s, rt))
        obj, log = compile_obj(cpp, d)
        if obj is None:
            stats["compile_error"] += 1
            bad.append(("COMPILE", path, s[:70], log.strip().splitlines()[-1][:160] if log.strip() else ""))
            continue
        stats["checked"] += 1
        fs = fresh_signature(obj)
        ss = stored_signature(c)
        if fs is None:
            stats["no_code"] += 1
            continue
        if fs[0] == ss[0] and fs[1] == ss[1]:
            stats["MATCH"] += 1
        elif fs[0] == ss[0]:
            stats["code_same_relocs_diff"] += 1
            bad.append(("RELOCS", path, s[:70], "fresh=%s\n          stored=%s" % (fs[1][:6], ss[1][:6])))
        else:
            stats["CODE_DIFF"] += 1
            bad.append(("CODE", path, s[:70],
                        "fresh len=%d stored len=%d\n          fresh=%s\n          stored=%s" %
                        (len(fs[0]), len(ss[0]), fs[0].hex(" ")[:200], ss[0].hex(" ")[:200])))
        if not args.keep:
            for f in os.listdir(d):
                os.remove(os.path.join(d, f))
    print("просмотрено формул:", n)
    for k, v in sorted(stats.items()):
        print("  %-22s %d" % (k, v))
    print("\n--- проблемы (%d) ---" % len(bad))
    for kind, path, s, info in bad[:60]:
        print("%-7s %s\n        src=%s\n        %s" % (kind, path, s, info))


if __name__ == "__main__":
    main()
