#!/usr/bin/env python3
# probe.py — спайк по переводу HLSL клиента в SPIR-V (PLAN_NVRHI §3, этап 3).
#
# Прогоняет DXC (vendor/dxc) по материалам из MaterialSpec.xml с той же
# перестановкой PIN'ов, что и боевой генератор, и классифицирует результат:
# сколько материалов компилируются в SPIR-V БЕЗ правки исходника, а какие
# конструкции для этого придётся переписывать.
#
# Запуск: ./probe.py [--only NAME] [--vs-only|--ps-only] [--full-pins]
import os, re, sys, subprocess, collections, xml.etree.ElementTree as ET
import shim

HERE = os.path.dirname(os.path.abspath(__file__))
R1117 = os.path.abspath(os.path.join(HERE, "..", "..", "pw", "branches", "r1117"))
SH = os.path.join(R1117, "Src", "Render", "Shaders")
RENDER = os.path.join(R1117, "Src", "Render")
DXC = os.path.join(HERE, "vendor", "dxc", "bin", "dxc")
ENV = dict(os.environ, LD_LIBRARY_PATH=os.path.join(HERE, "vendor", "dxc", "lib"))

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

SHIM = "--shim" in sys.argv
SHIM_DIR = None  # задаётся в build_shim_tree(stage)

PS_RET = ()
VS_RET = ()

def build_shim_tree(stage):
    global PS_RET, VS_RET
    if stage == "ps":
        names = set()
        for fn in os.listdir(SH):
            if fn.endswith(".hlsl"):
                t = open(os.path.join(SH, fn), encoding="utf-8", errors="replace").read()
                for m in re.finditer(r"^\s*(\w+)\s+\w+_PSMain\s*\(", t, re.M):
                    if m.group(1) not in ("float4", "float3", "float", "half4", "half3", "half"):
                        names.add(m.group(1))
        PS_RET = tuple(sorted(names))
    if stage == "vs":
        names = set()
        for fn in os.listdir(SH):
            if fn.endswith(".hlsl"):
                t = open(os.path.join(SH, fn), encoding="utf-8", errors="replace").read()
                for m in re.finditer(r"^\s*(\w+)\s+\w+_VSMain\s*\(", t, re.M):
                    if m.group(1) not in ("float4", "float3", "float", "VS_OUT", "VS_OUTPUT"):
                        names.add(m.group(1))
                    names.add(m.group(1))
        VS_RET = tuple(sorted(names))
    # Прослойка должна применяться и к включаемым файлам (_common.hlsl и др.),
    # поэтому копируем каталог шейдеров целиком: zzz_probe_shaders лежит рядом
    # с Shaders, так что #include "../GlobalMasks.h" продолжает работать.
    global SHIM_DIR
    SHIM_DIR = os.path.join(RENDER, "zzz_probe_shaders_" + stage)
    os.makedirs(SHIM_DIR, exist_ok=True)
    for fn in os.listdir(SH):
        src = os.path.join(SH, fn)
        if not os.path.isfile(src):
            continue
        if fn.endswith(".hlsl"):
            out, _st, _n = shim.rewrite(open(src, encoding="utf-8", errors="replace").read(), stage, PS_RET, VS_RET)
            open(os.path.join(SHIM_DIR, fn), "w", encoding="utf-8").write(out)
        elif fn.endswith(".h"):
            open(os.path.join(SHIM_DIR, fn), "wb").write(open(src, "rb").read())
    return SHIM_DIR

def run(path, stage, entry, defines):
    if SHIM:
        path = os.path.join(SHIM_DIR, os.path.basename(path))
    inc = SHIM_DIR if SHIM else SH
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv",
           "-I", inc, "-I", RENDER]
    for d in defines:
        cmd += ["-D", d]
    cmd += [path, "-Fo", "/tmp/spirv_probe_out.spv"]
    p = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")

def classify(err):
    # группировка по тексту ошибки, а не по файлу: путь только мешает
    msgs = []
    for l in err.splitlines():
        m = re.search(r"error:\s*(.*)", l)
        if m:
            msgs.append(m.group(1).strip()[:90])
    if msgs:
        return msgs[0]
    return err.strip().splitlines()[0][:90] if err.strip() else "ok"

def main():
    args = sys.argv[1:]
    only = args[args.index("--only") + 1] if "--only" in args else None
    mats = spec_materials()
    stats = collections.Counter()
    fails = collections.Counter()
    detail = []
    for name, pins in mats:
        if only and only != name:
            continue
        path = os.path.join(SH, name + ".hlsl")
        if not os.path.exists(path):
            stats["нет исходника"] += 1
            continue
        vs, ps = entries(path)
        if not vs and not ps:
            stats["нет MainPass_VSMain/PSMain"] += 1
            continue
        # базовая перестановка: все PIN'ы = 0 (как минимальная в генераторе)
        defines = ["%s=0" % p for p, _ in pins]
        for stage, lst in (("vs", vs), ("ps", ps)):
            if not lst:
                continue
            global SHIM_DIR
            if SHIM:
                SHIM_DIR = build_shim_tree(stage)
            code, out = run(path, stage, lst[0] + "_" + stage.upper() + "Main", defines)
            key = "%s/%s" % (name, stage)
            if code == 0:
                stats["PASS " + key] += 1
            else:
                stats["FAIL " + key] += 1
                c = classify(out)
                fails[c] += 1
                detail.append((key, c))
    print("материалов в спецификации: %d   режим: %s" % (len(mats), "SHIM" if SHIM else "без прослойки"))
    print("PASS: %d   FAIL: %d" % (
        sum(v for k, v in stats.items() if k.startswith("PASS")),
        sum(v for k, v in stats.items() if k.startswith("FAIL"))))
    print("\nклассы ошибок (частота):")
    for c, n in fails.most_common(25):
        print("  %3d  %s" % (n, c))
    if detail:
        print("\nнеудачи по материалам:")
        for k, c in detail:
            print("  %-34s %s" % (k, c))

if __name__ == "__main__":
    main()
