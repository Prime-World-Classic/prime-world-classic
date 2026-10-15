#!/usr/bin/env python3
# hlsl_sm3_check.py — побайтовая сверка SM3-блобов «до/после» правки .hlsl.
#
# Зачем (шаг 10 очереди, docs/PLAN_NVRHI.md §3.z п.2): правки ради Vulkan
# (SSBO-формы динамических массивов) не должны менять путь D3D9 — там тот же
# исходник компилируется в vs_3_0/ps_3_0, а контент (.shd) перегенерировать
# дорого и нельзя без причины. Проверка: скомпилировать корпус тем же
# компилятором и точками входа, что у генератора, и сравнить md5 DXBC.
#
# Корпус и пины — те же, что у tools/shader_budget (MaterialSpec.xml), только
# компилятор другой: D3DCompile (d3dcompiler_47) через tools/hlsl_compile_test
# под Wine, профили vs_3_0/ps_3_0, флаги 0x400 (OPTIMIZATION_LEVEL3).
#
# Запуск:
#   python3 tools/hlsl_sm3_check/hlsl_sm3_check.py --save /tmp/sm3_before.json
#   ... правка .hlsl ...
#   python3 tools/hlsl_sm3_check/hlsl_sm3_check.py --compare /tmp/sm3_before.json
# Опции: --pins N (все пины = N; по умолчанию 1), --only NAME,
#        --materials A,B,C (список материалов из MaterialSpec.xml),
#        --stages vs|ps|both, --remote <ssh-хост>, --dll <d3dcompiler_47.dll>
# Встроенный в Wine d3dcompiler построен на vkd3d и на SM3 умеет не всё
# (AOEMaterial vs_3_0 → «E5017: Aborting due to not yet implemented feature:
# SM1 cast from bool to integer»). Лечится нативной MS-библиотекой: --dll
# <путь> передаёт её в LoadLibrary по полному пути, Wine грузит её как native.
# Замер 2026-10-15: SDK-ная и OS-ная d3dcompiler_47.dll (обе x64, привезены с
# tiny10) дают под Wine DXBC, побайтово совпадающий с полученным на ВМ
# (AOEMaterial vs_3_0 — 632 Б, md5 77c15e7c…). Тогда SM3-контур доступен целиком
# на Linux; без --dll прогон либо падает, либо врёт — нужен --remote.
# Единицы с включёнными пинами компилируются долго (BasicFXMaterial vs_3_0 при
# всех пинах = 1 — 2m55s и 1,4 МБ DXBC), поэтому для регулярной сверки берут
# только затронутые материалы (--materials), а не весь корпус.
#
# --profile affected — готовый набор «материал + пины, при которых живёт
# затронутая шагами 10/11 кодовая ветка» (см. AFFECTED ниже). Все остальные
# пины не определяются, т.е. остаются 0 — это реальные комбинации, в отличие от
# «все пины = 1», где часть материалов вообще не компилируется (AOEMaterial
# vs_3_0 при всех пинах = 1 даёт 0x80004001).

# (материал, строка пинов через запятую). Две записи одного материала — разные
# ветки одного файла (у BasicMaterial instanceData живёт и при
# LightingPinValue=LIGHTINGINSTANCEDSH, и при NewInstancing).
AFFECTED = [
    ("AOEMaterial",                "CustomizedGeometryPinValue=1"),
    ("BasicFXMaterial",            "SkeletalMeshPinValue=1"),
    ("BasicMaterial",              "InstancingValue=1"),
    ("BasicMaterial",              "InstancingValue=1,LightingPinValue=1"),
    ("DebugMaterial",              "SkeletalMeshPinValue=1"),
    ("DecalMaterial",              "InstancingValue=1"),
    ("DecalTerrainMaterial",       "InstancingValue=1"),
    ("DropMaterial",               "SkeletalMeshPinValue=1"),
    ("FullScreenMaterial",         "SharpPinValue=1"),
    ("FullScreenMaterial",         "SharpPinValue=1,ColorCorrectPinValue=1"),
    ("NatureMapMaterial",          ""),
    ("ParticleFXMaterial",         ""),
    ("ShadowMaterial",             "InstancingValue=1"),
    ("ShadowMaterial",             "SkeletalMeshPinValue=1"),
    ("SilhouetteMaterial",         "SkeletalMeshPinValue=1"),
    ("TerrainElementDebugMaterial", ""),
    # шаг 11: `volatile` в UIBaseMaterial (Stripes == PRESENT) — DXC его
    # резервирует, SM3-ветка осталась дословной. Профиль обязан расти вместе с
    # любой правкой исходника, иначе «SM3 побайтово не изменился» ничего не
    # доказывает для затронутой ветки пинов.
    ("UIBaseMaterial",             "Stripes=1"),
]
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
BR = os.path.join(ROOT, "pw", "branches", "r1117")
SH = os.path.join(BR, "Src", "Render", "Shaders")
HCT = os.path.join(TOOLS, "hlsl_compile_test", "build", "hlsl_compile_test.exe")
WINEPREFIX = os.environ.get("WINEPREFIX", os.path.expanduser("~/.wine-vs"))
OUTDIR = "/tmp/hlsl_sm3_check"
# --define NAME=ЗНАЧЕНИЕ (повторяемо) — дополнительные макросы шва исходников
EXTRA_DEFINES = [sys.argv[i + 1] for i, a in enumerate(sys.argv)
                 if a == "--define" and i + 1 < len(sys.argv)]
# нативная MS-библиотека компилятора вместо wine-ного builtin (vkd3d)
NATIVE_DLL = ""


def zpath(p):
    """Linux-путь -> путь Wine (диск Z: = корень ФС), иначе fopen не туда пишет."""
    return "Z:" + p.replace("/", "\\")


def spec_materials():
    root = ET.parse(os.path.join(SH, "MaterialSpec.xml")).getroot()
    out = []
    for item in root.iter("Item"):
        name = item.findtext("shaderName")
        if not name:
            continue
        pins = [(p.findtext("pinName"), int(p.findtext("pinMultiplier", "1")))
                for p in item.iter("Item") if p.findtext("pinName")]
        out.append((name, pins))
    return out


def entries(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    vs = re.findall(r"^\s*[\w:]+\s+(\w+)_VSMain\s*\(", src, re.M)
    ps = re.findall(r"^\s*[\w:]+\s+(\w+)_PSMain\s*\(", src, re.M)
    return vs, ps


def compile_one_remote(path, stage, entry, defines, dump):
    """Прогон на ВМ — настоящий d3dcompiler_47 от Microsoft.

    Альтернатива без ВМ — --dll <нативная d3dcompiler_47.dll> в локальном
    режиме: Wine грузит её как native, DXBC побайтово совпадает с ВМ-ным.
    Без обоих вариантов для vs_3_0 прогон неполон: компилятор Wine построен на
    vkd3d и умеет не всё. Пример: AOEMaterial vs_3_0 c
    CustomizedGeometryPinValue=1 в Wine даёт
    «E5017: Aborting due to not yet implemented feature: SM1 cast from bool to
    integer» (hr=0x80004001), на ВМ — OK, DXBC 632 Б.
    Дерево исходников на ВМ (--rroot): Src/Render/{Shaders,GlobalMasks.h}
    (собирается zip'ом из pw/branches/r1117/Src/Render, ~320 КБ)."""
    rfile = "%s\\%s" % (RROOT, os.path.relpath(path, BR).replace("/", "\\"))
    rinc = "%s\\%s" % (RROOT, os.path.relpath(SH, BR).replace("/", "\\"))
    rdump = "%s\\dumps\\%s" % (RROOT, os.path.basename(dump))
    # без shell=True: пути без пробелов, локальный shell не должен разбирать
    # «&» и кавычки — удалённую командную строку разбирает только cmd.exe на ВМ
    rcmd = ("%s %s %s %s --inc %s --flags 400 --dump %s %s"
            " & certutil -hashfile %s MD5") % (
        RTOOL, rfile, stage + "_3_0", entry, rinc, rdump,
        " ".join("--def %s" % d for d in defines), rdump)
    out = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8",
                          REMOTE, rcmd], capture_output=True, text=True).stdout
    hr, size, md5 = None, None, None
    m = re.search(r"D3DCompile hr=(0x[0-9a-fA-F]{8})", out)
    if m:
        hr = m.group(1)
    m = re.search(r"DXBC: (\d+) байт", out)
    if m:
        size = int(m.group(1))
    m = re.search(r"\b([0-9a-fA-F]{32})\b", out)
    if m:
        md5 = m.group(1).lower()
    if md5 is None:
        return {"hr": hr, "size": size, "md5": None,
                "err": " ".join(out.strip().splitlines())[:200]}
    return {"hr": hr, "size": size, "md5": md5}


def compile_one(path, stage, entry, defines, dump):
    profile = stage + "_3_0"
    if REMOTE:
        return compile_one_remote(path, stage, entry, defines, dump)
    out_file = dump + ".log"
    cmd = ["wine", zpath(HCT), zpath(path), profile, entry, "--inc", zpath(SH),
           "--flags", "400", "--dump", zpath(dump)]
    for d in defines:
        cmd += ["--def", d]
    if NATIVE_DLL:
        cmd += ["--dll", zpath(NATIVE_DLL)]
    env = dict(os.environ, WINEPREFIX=WINEPREFIX, WINEDEBUG="-all")
    # stdout в файл, не в PIPE: Wine пишет консольный вывод программы мимо
    # пайпа, и через capture_output сообщения компилятора теряются.
    with open(out_file, "wb") as fh:
        p = subprocess.run(cmd, env=env, stdout=fh, stderr=subprocess.STDOUT,
                           timeout=900)
    out = open(out_file, encoding="utf-8", errors="replace").read()
    os.remove(out_file)
    hr = None
    m = re.search(r"D3DCompile hr=0x([0-9a-fA-F]{8})", out)
    if m:
        hr = "0x" + m.group(1)
    size = None
    m = re.search(r"DXBC: (\d+) байт", out)
    if m:
        size = int(m.group(1))
    md5 = None
    if os.path.exists(dump):
        md5 = hashlib.md5(open(dump, "rb").read()).hexdigest()
        os.remove(dump)
    if md5 is None:
        err = re.search(r"error [^\n]*", out)
        return {"hr": hr, "size": size, "md5": None,
                "err": err.group(0)[:160] if err else out.strip()[:160]}
    return {"hr": hr, "size": size, "md5": md5}


def main():
    args = sys.argv[1:]
    save = args[args.index("--save") + 1] if "--save" in args else None
    compare = args[args.index("--compare") + 1] if "--compare" in args else None
    pinval = int(args[args.index("--pins") + 1]) if "--pins" in args else 1
    only = args[args.index("--only") + 1] if "--only" in args else None
    mats_filter = args[args.index("--materials") + 1].split(",") if "--materials" in args else None
    stages = args[args.index("--stages") + 1] if "--stages" in args else "both"
    global REMOTE, RROOT, RTOOL
    REMOTE = args[args.index("--remote") + 1] if "--remote" in args else ""
    RROOT = args[args.index("--rroot") + 1] if "--rroot" in args else "C:\\pwbuild\\shaders"
    RTOOL = args[args.index("--rtool") + 1] if "--rtool" in args else "C:\\tools\\hlsl\\hlsl_compile_test.exe"
    global NATIVE_DLL
    NATIVE_DLL = args[args.index("--dll") + 1] if "--dll" in args else ""
    if NATIVE_DLL and not os.path.exists(NATIVE_DLL):
        sys.exit("нет %s — нативная d3dcompiler_47.dll (x64) берётся с Windows-хоста:\n"
                 "  C:\\Windows\\System32\\D3DCompiler_47.dll или "
                 "C:\\pwbuild\\vs2022\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\" % NATIVE_DLL)
    if REMOTE:
        subprocess.run('ssh -o BatchMode=yes %s "mkdir %s\\dumps"' % (REMOTE, RROOT),
                       shell=True, capture_output=True)
    if not os.path.exists(HCT):
        sys.exit("нет %s — собрать tools/hlsl_compile_test/build.sh" % HCT)
    os.makedirs(OUTDIR, exist_ok=True)

    res = {}
    profile = "--profile" in args
    units_iter = []
    if profile:
        for name, pinstr in AFFECTED:
            if mats_filter and name not in mats_filter:
                continue
            units_iter.append((name, [p for p in pinstr.split(",") if p]))
    else:
        for name, pins in spec_materials():
            if only and only != name:
                continue
            if mats_filter and name not in mats_filter:
                continue
            units_iter.append((name, ["%s=%d" % (p, pinval) for p, _ in pins]))
    for name, pindefs in units_iter:
        if not profile and only and only != name:
            continue
        path = os.path.join(SH, name + ".hlsl")
        if not os.path.exists(path):
            print("нет файла: %s" % path)
            continue
        vs, ps = entries(path)
        defines = list(pindefs) + EXTRA_DEFINES
        pinsuffix = ("_" + "_".join(sorted(pindefs))) if profile else ""
        for stage, lst in (("vs", vs), ("ps", ps)):
            if stages != "both" and stages != stage:
                continue
            for base in lst:
                # имя точки входа у генератора = <Pass>_VSMain / <Pass>_PSMain,
                # регулярка выше снимает только префикс <Pass>
                entry = base + "_" + stage.upper() + "Main"
                key = "%s/%s%s" % (name, entry, pinsuffix)
                dump = os.path.join(OUTDIR, "%s_%s%s.dxbc" % (name, entry, pinsuffix))
                r = compile_one(path, stage, entry, defines, dump)
                res[key] = r
                print("%-34s %-22s %-34s hr=%s size=%-7s md5=%s%s" % (
                    name, entry, ",".join(pindefs), r["hr"], r["size"],
                    (r["md5"] or "-")[:12],
                    ("  " + r["err"]) if r.get("err") else ""), flush=True)

    if save:
        json.dump({"pins": pinval, "units": res}, open(save, "w"), indent=1)
        print("\nсохранено: %s (%d единиц)" % (save, len(res)))
    if compare:
        old = json.load(open(compare))
        if old.get("pins") != pinval:
            print("ВНИМАНИЕ: pins в снимке = %s, сейчас = %d" % (old.get("pins"), pinval))
        same, changed, gone, new = [], [], [], []
        for k, r in res.items():
            o = old["units"].get(k)
            if o is None:
                new.append(k)
            elif o.get("md5") == r.get("md5") and (o.get("md5") is not None
                                                   or o.get("hr") == r.get("hr")):
                # одинаковый DXBC, либо одинаковый провал компиляции (оба hr != 0)
                same.append(k)
            else:
                changed.append((k, o, r))
        for k in old["units"]:
            if k not in res:
                gone.append(k)
        print("\nодинаковых DXBC: %d, изменившихся: %d, пропало единиц: %d, новых: %d"
              % (len(same), len(changed), len(gone), len(new)))
        for k, o, r in changed:
            print("  ИЗМЕНИЛОСЬ %-34s %s -> %s  (%s -> %s)" % (
                k, o.get("md5"), r.get("md5"), o.get("size"), r.get("size")))
        for k in gone:
            print("  ПРОПАЛА    %s (было hr=%s)" % (k, old["units"][k].get("hr")))
        for k in new:
            print("  НОВАЯ      %s (md5=%s)" % (k, res[k].get("md5")))
        if changed or gone or new:
            print("\nИТОГ: SM3-путь изменился — нужна перегенерация .shd и визуальный гейт")
            return 1
        print("\nИТОГ: SM3-путь побайтово не изменился")
    return 0


if __name__ == "__main__":
    sys.exit(main())
