#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_blob.py — генерирует <compiledString> из <sString> (порт
Src/FormulaBuilder/FormulaBuilder.cpp: PrepareCFile + CompileObjFile +
ExtractCodeFromFile), т.е. тем же конвейером, которым собран контент.

Зачем: правка sString без пересборки блока расходится с тем, что исполняет x86
(DefaultFormulaStorage предпочитает compiledString). Чтобы чинить такие расхождения
честно (например снять контент-баг MagicGirl A1u), нужен генератор блока.

Доказательство корректности генератора — самопроверка: для формул контента, у которых
блок уже есть, сгенерированный блок обязан совпасть ПОБАЙТОВО:
  python3 Tools/FormulaCheck/build_blob.py Data --selftest 200
Побайтовое совпадение подтверждает и формат FormulaHeader, и таблицу внешних символов
(ExecutionMemoryManager::pLinkTable), и совпадение компилятора/ключей.

Порт алгоритма ExtractCodeFromFile (порядок шагов и порядок записи релокаций важны):
  1) конкатенация секций CODE|INITIALIZED_DATA (кроме .debug*) в образ;
     sectionsOffsets[i] — позиция в образе ДО секции i (в т.ч. пропущенных);
  2) релокации DIR32 -> EXT_DIRECT, REL32 -> EXT_RELATIVE, прочие = ошибка;
  3) backPatch = все DIR32, собранные ОБРАТНЫМ обходом списка релокаций;
  4) внутренняя линковка (символ найден в образе): DIR32 = base+sym,
     REL32 = base+sym-(off+4); запись в образ, запись удаляется из списка;
  5) внешние: ExecutionMemoryManager::GetSymbolOffset(name) =
     (индексВpLinkTable - DIM(pLinkTable)) * sizeof(JumpAbsoluteFar);
     REL32 = nOffset-(off+4), попадание в extBackPatch (DIR32 внешняя в пайплайне
     помечена NI_ALWAYS_ASSERT "Not yet supported" — в контенте таких нет);
  6) точка входа — первый найденный из _cpEntryPointNames (mangled abilityFunctor:
     float/int/bool с IUnitFormulaPars-вторым аргументом и вариант с ICustomFormulaPars);
  7) header(version, entryPointOffset, relocsNum, extRelocsNum) + смещения backPatch
     (обратный обход вектора = исходный порядок) + смещения extBackPatch (аналогично)
     + образ.

Запуск (из pw/branches/r1117):
  python3 Tools/FormulaCheck/build_blob.py Data --selftest 200 [--dir X] [--work DIR] [-v]
  python3 Tools/FormulaCheck/build_blob.py --gen "<sString>" --ret float
"""
import argparse
import base64
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
R1117 = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import verify_formulas as VF          # noqa: E402
import recompile_check as RC          # noqa: E402

HEADER_FMT = VF.HEADER_FMT             # "<BIHH": version, entry, relocsNum, extRelocsNum
FORMULA_VERSION = VF.FORMULA_VERSION   # 21

# FormulaBuilder.cpp:30 _cpEntryPointNames
ENTRY_NAMES = [
    "?abilityFunctor@@YAMPBUIUnitFormulaPars@@0PBUIMiscFormulaPars@@@Z",
    "?abilityFunctor@@YAHPBUIUnitFormulaPars@@0PBUIMiscFormulaPars@@@Z",
    "?abilityFunctor@@YA_NPBUIUnitFormulaPars@@0PBUIMiscFormulaPars@@@Z",
    "?abilityFunctor@@YA_NPBUIUnitFormulaPars@@PBUICustomFormulaPars@@PBUIMiscFormulaPars@@@Z",
]

# ExecutionMemoryManager.cpp pLinkTable (порядок значим)
LINK_TABLE = ["_sin", "_cos", "_sqrt", "_pow", "_fmod", "_fabs", "_floor", "_ceil"]
LINK_SIZE = 6                          # sizeof(JumpAbsoluteFar) при pack(1)

# <returnType> в контенте — не всегда C++-имя: "boolean" -> bool (вызывающая
# сторона пайплайна передаёт в FormulaBuilder уже C++-тип)
RET_CPP = {"float": "float", "int": "int", "bool": "bool", "boolean": "bool"}

IMAGE_REL_I386_DIR32 = 0x06
IMAGE_REL_I386_REL32 = 0x14
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040


class BuildError(Exception):
    pass


def find_symbol(img, name):
    """GetSymbolSectionOffset: (value, 1-based section index) или (None, None)."""
    for s in img.symbols:
        if s["name"] == name and 0 < s["sec"] <= img.nsec:
            return s["value"], s["sec"]
    return None, None


def link_obj(img):
    """ExtractCodeFromFile -> (образ, entry, backPatchOffsets, extBackPatchOffsets)."""
    buf = bytearray()
    sec_off = []
    relocs = []
    for sec in img.sections:
        sec_off.append(len(buf))
        fl = sec["flags"]
        if not (fl & IMAGE_SCN_CNT_CODE) and not (fl & IMAGE_SCN_CNT_INITIALIZED_DATA):
            continue
        if sec["name"].startswith(".debug"):
            continue
        for r in sec["rels"]:
            typ = r["type"]
            if typ not in (IMAGE_REL_I386_DIR32, IMAGE_REL_I386_REL32):
                raise BuildError("unsupported reloc type 0x%x" % typ)
            relocs.append((len(buf) + r["offs"], img.symbols[r["sym"]]["name"], typ))
        buf += sec["raw"]

    back = [r for r in reversed(relocs) if r[2] == IMAGE_REL_I386_DIR32]

    unresolved = []
    for off, name, typ in reversed(relocs):
        val, sec_idx = find_symbol(img, name)
        if val is None:
            unresolved.append((off, name, typ))
            continue
        base = sec_off[sec_idx - 1]
        if typ == IMAGE_REL_I386_DIR32:
            struct.pack_into("<i", buf, off, base + val)
        else:
            struct.pack_into("<i", buf, off, base + val - (off + 4))

    ext = []
    for off, name, typ in unresolved:
        if name not in LINK_TABLE:
            raise BuildError("symbol %s could not be dynamically linked "
                             "(not in DataExecutor::pLinkTable)" % name)
        n = (LINK_TABLE.index(name) - len(LINK_TABLE)) * LINK_SIZE
        if typ == IMAGE_REL_I386_DIR32:
            raise BuildError("external DIR32 relocation (%s) is not supported "
                             "by the pipeline" % name)
        struct.pack_into("<i", buf, off, n - (off + 4))
        ext.append(off)

    entry = None
    for nm in ENTRY_NAMES:
        val, sec_idx = find_symbol(img, nm)
        if val is not None:
            entry = sec_off[sec_idx - 1] + val
            break
    if entry is None:
        raise BuildError("entry point abilityFunctor not found in compiled snippet")

    return bytes(buf), entry, [r[0] for r in reversed(back)], list(reversed(ext))


def compile_cpp(cpp_path, out_dir):
    """как FormulaBuilder::CompileObjFile: cl -I"<Data/GameLogic>" -I"<Tools/CCompiler>" -c file.cpp;
    запуск из каталога файла (Wine не понимает абсолютные Linux-пути), без -Fo."""
    env = dict(os.environ, WINEPREFIX=RC.WINEPREFIX, WINEDEBUG="-all",
               WINEDLLOVERRIDES="mscoree,mshtml=")
    cmd = ["wine", RC.CL, '-I"%s"' % RC.INC1, '-I"%s"' % RC.INC2,
           "-c", os.path.basename(cpp_path)]
    r = subprocess.run(cmd, cwd=out_dir, env=env, capture_output=True, timeout=300)
    log = (r.stdout + r.stderr).decode("cp1251", "replace")
    if r.returncode != 0:
        return None, log
    return cpp_path[:-4] + ".obj", log


def build_blob(sstring, ret, work, alt_second=False):
    """sString -> bytes(FormulaHeader). Точно тем же компилятором и с теми же
    ключами, что FormulaBuilder::CompileObjFile."""
    os.makedirs(work, exist_ok=True)
    cpp = os.path.join(work, "formula.cpp")
    with open(cpp, "w", encoding="utf-8") as f:
        f.write(RC.gen_cpp(sstring, RET_CPP.get(ret, ret), alt_second))
    obj, log = compile_cpp(cpp, work)
    if obj is None:
        raise BuildError("compile failed: %s" % log.strip()[:400])
    img = RC.Coff(obj)
    return _link_and_pack(img)


def build_blob_auto(sstring, ret, work):
    """Как пайплайн: вызывающая сторона знает контекст и передаёт altSecond
    (pSecond как ICustomFormulaPars — FormulaBuilder.cpp:30, 4-е имя точки входа).
    Здесь контекст неизвестен, поэтому пробуем обычный вариант, а при ошибке
    компиляции — alt. О том, какой вариант сработал, судим по имени точки входа."""
    try:
        return build_blob(sstring, ret, work, False), False
    except BuildError as e:
        if "compile failed" not in str(e):
            raise
        return build_blob(sstring, ret, work, True), True


def _link_and_pack(img):
    image, entry, back, ext = link_obj(img)
    blob = struct.pack(HEADER_FMT, FORMULA_VERSION, entry, len(back), len(ext))
    for o in back:
        blob += struct.pack("<I", o)
    for o in ext:
        blob += struct.pack("<I", o)
    blob += image
    return blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", help="Data-корень (для --selftest)")
    ap.add_argument("--selftest", type=int, default=0, metavar="N")
    ap.add_argument("--stride", type=int, default=1,
                    help="пропускать формулы (сэмплирование по всему дереву)")
    ap.add_argument("--grep", help="фильтр по подстроке пути (для самопроверки точечно)")
    ap.add_argument("--dir")
    ap.add_argument("--work", default="/tmp/fblob")
    ap.add_argument("--gen", metavar="sString")
    ap.add_argument("--ret", default="float")
    ap.add_argument("--alt-second", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    if a.gen:
        blob = build_blob(a.gen, a.ret, a.work, a.alt_second)
        print("len=%d entry=%d relocs=%d ext=%d" %
              (len(blob), struct.unpack_from("<BIHH", blob, 0)[1],
               struct.unpack_from("<BIHH", blob, 0)[2],
               struct.unpack_from("<BIHH", blob, 0)[3]))
        print(base64.b64encode(blob).decode())
        return

    if not a.selftest or not a.root:
        ap.print_help()
        return 2

    work = tempfile.mkdtemp(prefix="fblob_", dir=a.work if os.path.isdir(a.work) else None)
    stat = {"same": 0, "diff": 0, "cerr": 0, "linkerr": 0, "len_diff": 0}
    shown = 0
    n = 0
    checked = 0
    for path, s, c, rt in VF.iter_formulas(a.root, a.dir):
        if not c or not s.strip():
            continue
        if a.grep and a.grep not in path:
            continue
        n += 1
        if n % a.stride:
            continue
        if checked >= a.selftest:
            break
        stored = base64.b64decode(c)
        checked += 1
        try:
            gen, _alt = build_blob_auto(s, rt, work)
        except BuildError as e:
            msg = str(e)
            if "compile failed" in msg:
                stat["cerr"] += 1
            else:
                stat["linkerr"] += 1
            if shown < 5:
                shown += 1
                print("ERR  %s\n     %s | %s" % (path, rt, s[:90]))
                print("     %s" % msg[:200])
            continue
        if gen == stored:
            stat["same"] += 1
        else:
            stat["diff"] += 1
            if len(gen) != len(stored):
                stat["len_diff"] += 1
            if shown < 5:
                shown += 1
                print("DIFF %s\n     %s | %s" % (path, rt, s[:90]))
                print("     gen len=%d stored len=%d" % (len(gen), len(stored)))
                for i in range(min(len(gen), len(stored))):
                    if gen[i] != stored[i]:
                        print("     first diff at %d: gen=%02x stored=%02x" %
                              (i, gen[i], stored[i]))
                        break
                if a.verbose:
                    print("     gen   =%s" % gen[:64].hex())
                    print("     stored=%s" % stored[:64].hex())
    shutil.rmtree(work, ignore_errors=True)
    print("\nпроверено: %d" % checked)
    for k in ("same", "diff", "cerr", "linkerr"):
        print("  %-8s %d" % (k, stat[k]))
    print("  (из diff — разница длин: %d)" % stat["len_diff"])
    return 0 if stat["diff"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
