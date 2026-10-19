#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Офлайн-чекер вершинного/пиксельного интерфейса по записи PWRD.

Зачем. D3D9 сопоставляет varying'и по ИМЕНИ семантики, Vulkan — по LOCATION.
DXC назначает location каждому шейдеру НЕЗАВИСИМО (по порядку объявления в
структуре входа/выхода; неиспользованные поля вырезаются мёртвым кодом, но
свой номер занимают). Поэтому у пары VS/PS один и тот же TEXCOORD3 легко
оказывается на разных location, а у VS остаётся «дырка», которой нет в
вершинной декларации. Отсюда три VUID из валидатора:

  VUID-VkGraphicsPipelineCreateInfo-Input-07904  вход VS есть, в layout нет
  VUID-VkGraphicsPipelineCreateInfo-Input-08733  класс формата != тип входа VS
  VUID-RuntimeSpirv-OpEntryPoint-08743           вход PS есть, выхода VS нет

Парсер SPIR-V — через spirv-dis (SPIRV-Tools), своего декодера нет.

Использование:
  iface_check.py ФАЙЛ.pwrd [--pair VS:PS ...] [-v]
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_pwrd import parse, KNO  # noqa: E402

# nvrhi::Format (Vendor/NVRHI/include/nvrhi/nvrhi.h:161) — только класс и ширина
FMT = [
    "UNKNOWN", "R8_UINT", "R8_SINT", "R8_UNORM", "R8_SNORM", "RG8_UINT",
    "RG8_SINT", "RG8_UNORM", "RG8_SNORM", "R16_UINT", "R16_SINT", "R16_UNORM",
    "R16_SNORM", "R16_FLOAT", "BGRA4_UNORM", "B5G6R5_UNORM", "B5G5R5A1_UNORM",
    "RGBA8_UINT", "RGBA8_SINT", "RGBA8_UNORM", "RGBA8_SNORM", "BGRA8_UNORM",
    "BGRX8_UNORM", "SRGBA8_UNORM", "SBGRA8_UNORM", "SBGRX8_UNORM",
    "R10G10B10A2_UNORM", "R11G11B10_FLOAT", "RG16_UINT", "RG16_SINT",
    "RG16_UNORM", "RG16_SNORM", "RG16_FLOAT", "R32_UINT", "R32_SINT",
    "R32_FLOAT", "RGBA16_UINT", "RGBA16_SINT", "RGBA16_FLOAT", "RGBA16_UNORM",
    "RGBA16_SNORM", "RG32_UINT", "RG32_SINT", "RG32_FLOAT", "RGB32_UINT",
    "RGB32_SINT", "RGB32_FLOAT", "RGBA32_UINT", "RGBA32_SINT", "RGBA32_FLOAT",
    "D16", "D24S8", "X24G8_UINT", "D32", "D32S8", "X32G8_UINT",
]


def fmt_class(f):
    """Класс числовых данных по имени nvrhi::Format (для VUID-08733)."""
    n = FMT[f] if f < len(FMT) else "?"
    if n.endswith("_UINT"):
        return "uint"
    if n.endswith("_SINT"):
        return "sint"
    return "float"


def fmt_components(f):
    n = FMT[f] if f < len(FMT) else "?"
    if n.startswith("RGBA"):
        return 4
    if n.startswith("RGB"):
        return 3
    if n.startswith("RG"):
        return 2
    if n.startswith("R"):
        return 1
    if n in ("BGRA4_UNORM", "B5G5R5A1_UNORM", "R10G10B10A2_UNORM"):
        return 4
    if n in ("B5G6R5_UNORM", "R11G11B10_FLOAT"):
        return 3
    return 0


ID = r"[%a-zA-Z_][%\w.]*"
RE_NAME = re.compile(r"^\s*OpName\s+(%s)\s+\"(.*?)\"" % ID)
RE_ENTRY = re.compile(r"^\s*OpEntryPoint\s+(\w+)\s+(%s)\s+\"(.*?)\"(.*)$" % ID)
RE_LOC = re.compile(r"^\s*OpDecorate\s+(%s)\s+Location\s+(\d+)(?:\s+(\d+))?" % ID)
RE_BUILTIN = re.compile(r"^\s*OpDecorate\s+(%s)\s+BuiltIn\s+(\w+)" % ID)
RE_VAR = re.compile(r"^\s*(%s)\s*=\s*OpVariable\s+(%s)\s+(Input|Output|Uniform|StorageBuffer|StorageImage|SampledImage|Function|Private|PushConstant)" % (ID, ID))
RE_PTR = re.compile(r"^\s*(%s)\s*=\s*OpTypePointer\s+(\w+)\s+(%s)" % (ID, ID))
RE_VEC = re.compile(r"^\s*(%s)\s*=\s*OpTypeVector\s+(%s)\s+(\d+)" % (ID, ID))
RE_FLOAT = re.compile(r"^\s*(%s)\s*=\s*OpTypeFloat\s+(\d+)" % ID)
RE_INT = re.compile(r"^\s*(%s)\s*=\s*OpTypeInt\s+(\d+)\s+(\d+)" % ID)
RE_MAT = re.compile(r"^\s*(%s)\s*=\s*OpTypeMatrix\s+(%s)\s+(\d+)" % (ID, ID))
RE_ARR = re.compile(r"^\s*(%s)\s*=\s*OpTypeArray\s+(\S+)\s+(\S+)" % ID)
RE_IMG = re.compile(r"^\s*(%s)\s*=\s*OpTypeImage\s+" % ID)
RE_SIMG = re.compile(r"^\s*(%s)\s*=\s*OpTypeSampledImage\s+(%s)" % (ID, ID))
RE_STR = re.compile(r"^\s*(%s)\s*=\s*OpTypeStruct(.*)$" % ID)


def disasm(spv_bytes):
    with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as f:
        f.write(spv_bytes)
        p = f.name
    try:
        out = subprocess.run(["spirv-dis", "--no-header", p],
                             capture_output=True, text=True)
        if out.returncode != 0:
            return None, out.stderr.strip()
        return out.stdout, None
    finally:
        os.unlink(p)


def iface(spv):
    """Интерфейс шейдера: {'stage':.., 'in': {loc: {...}}, 'out': {...}}"""
    txt, err = disasm(spv)
    if txt is None:
        return None, err
    names, types, ptrs = {}, {}, {}
    res = {}
    vec, mat, arr, img, simg, structs = set(), set(), set(), set(), set(), set()
    floats, ints = {}, {}
    loc, builtin, entry = {}, {}, None
    stage = None
    for line in txt.split("\n"):
        m = RE_NAME.match(line)
        if m:
            names[m.group(1)] = m.group(2)
            continue
        m = RE_ENTRY.match(line)
        if m:
            stage = m.group(1)
            entry = m.group(2)
            continue
        m = RE_LOC.match(line)
        if m:
            loc[m.group(1)] = (int(m.group(2)), int(m.group(3) or 0))
            continue
        m = RE_BUILTIN.match(line)
        if m:
            builtin[m.group(1)] = m.group(2)
            continue
        m = RE_PTR.match(line)
        if m:
            ptrs[m.group(1)] = (m.group(2), m.group(3))
            continue
        m = RE_VEC.match(line)
        if m:
            vec.add(m.group(1))
            types[m.group(1)] = ("vec", m.group(2), int(m.group(3)))
            continue
        m = RE_MAT.match(line)
        if m:
            mat.add(m.group(1))
            types[m.group(1)] = ("mat", m.group(2), int(m.group(3)))
            continue
        m = RE_FLOAT.match(line)
        if m:
            floats[m.group(1)] = int(m.group(2))
            types[m.group(1)] = ("float", None, 1)
            continue
        m = RE_INT.match(line)
        if m:
            ints[m.group(1)] = int(m.group(3))
            types[m.group(1)] = ("sint" if m.group(3) == "1" else "uint",
                                 None, 1)
            continue
        m = RE_ARR.match(line)
        if m:
            arr.add(m.group(1))
            types[m.group(1)] = ("array", m.group(2), None)
            continue
        m = RE_IMG.match(line)
        if m:
            img.add(m.group(1))
            types[m.group(1)] = ("image", None, None)
            continue
        m = RE_SIMG.match(line)
        if m:
            simg.add(m.group(1))
            types[m.group(1)] = ("sampled", m.group(2), None)
            continue
        m = RE_STR.match(line)
        if m:
            structs.add(m.group(1))
            types[m.group(1)] = ("struct", None, None)
            continue
        m = RE_VAR.match(line)
        if m:
            var, ptr, sc = m.group(1), m.group(2), m.group(3)
            if sc not in ("Input", "Output"):
                continue
            p = ptrs.get(ptr)
            base = p[1] if p else None
            t = types.get(base, ("?", None, None))
            semantic = names.get(var, var)
            # DXC: "in.var.TEXCOORD3" / "out.var.SV_TARGET"
            semantic = semantic.split(".")[-1]
            l = loc.get(var)
            entry_kind = "in" if sc == "Input" else "out"
            res.setdefault(entry_kind, {})[var] = {
                "loc": None if l is None else l[0],
                "comp": 0 if l is None else l[1],
                "semantic": semantic,
                "type": t[0], "compType": t[1], "n": t[2],
                "builtin": builtin.get(var),
            }
    return {"stage": stage, "entry": entry, "in": res.get("in", {}),
            "out": res.get("out", {})}, None


def sem_of_attr(a):
    return a["name"]


def check(pwrd_path, verbose=False):
    hdr, shaders, buffers, textures, layouts, draws, _ = parse(pwrd_path)
    print("PWRD v%d  выводов=%d шейдеров=%d раскладок=%d" %
          (hdr["ver"], len(draws), len(shaders), len(layouts)))
    cache = {}

    def get(sid):
        if sid in cache:
            return cache[sid]
        r = iface(shaders[sid]["spv"])
        if r[1]:
            print("spirv-dis не разобрал шейдер %d: %s" % (sid, r[1]))
        cache[sid] = r[0]
        return cache[sid]

    problems = collections.Counter()
    seen_pairs = {}
    for d in draws:
        vs, ps = get(d["vs"]) if d["vs"] != KNO else None, \
            get(d["ps"]) if d["ps"] != KNO else None
        lay = layouts.get(d["layout"])
        if vs is None or ps is None:
            continue
        key = (d["vs"], d["ps"])
        if key in seen_pairs and not verbose:
            continue
        seen_pairs[key] = d["n"]
        vs_out = {v["loc"]: v for v in vs["out"].values() if v["loc"] is not None
                  and not v["builtin"]}
        ps_in = {v["loc"]: v for v in ps["in"].values() if v["loc"] is not None
                 and not v["builtin"]}
        vs_in = {v["loc"]: v for v in vs["in"].values() if v["loc"] is not None
                 and not v["builtin"]}
        attrs = lay["attrs"] if lay else []
        # 08743: вход PS на location, которого нет среди выходов VS
        for l, v in sorted(ps_in.items()):
            if l not in vs_out:
                problems["08743"] += 1
                near = [o for o in vs_out.values() if o["semantic"] == v["semantic"]]
                print("draw#%-4d 08743 PS %s на Location %d, у VS выхода нет"
                      " (у VS этот семантик %s)" %
                      (d["n"], v["semantic"], l,
                       "на Location %d" % near[0]["loc"] if near else "не выходит"))
        # 07904: вход VS на location, которого нет в массиве атрибутов
        for l, v in sorted(vs_in.items()):
            if l >= len(attrs):
                problems["07904"] += 1
                print("draw#%-4d 07904 VS %s на Location %d, в layout только "
                      "%d атр: %s" % (d["n"], v["semantic"], l, len(attrs),
                                      ",".join(a["name"] for a in attrs)))
        # 08733: класс формата атрибута против типа входа VS
        for i, a in enumerate(attrs):
            v = vs_in.get(i)
            if not v:
                continue
            want = fmt_class(a["format"])
            got = v["type"]
            if got in ("sint", "uint") and want != got:
                problems["08733"] += 1
                print("draw#%-4d 08733 атр[%d] %s fmt=%s(%d), SPIR-V ждёт %s"
                      % (d["n"], i, a["name"], FMT[a["format"]], a["format"], got))
            if got == "float" and want in ("sint", "uint"):
                problems["08733"] += 1
                print("draw#%-4d 08733 атр[%d] %s fmt=%s(%d) — целочисленный,"
                      " SPIR-V ждёт float" %
                      (d["n"], i, a["name"], FMT[a["format"]], a["format"]))
            n = fmt_components(a["format"])
            if v["type"] == "vec" and v["n"] and n and v["n"] != n:
                problems["width"] += 1
                print("draw#%-4d ширина атр[%d] %s: fmt %s даёт %d компонента,"
                      " шейдер ждёт vec%d" %
                      (d["n"], i, a["name"], FMT[a["format"]], n, v["n"]))
        # PS пишет в location, которого нет среди цветовых прикреплений
        # (у клиента одно приложение + depth)
        for l, v in sorted(((x["loc"], x) for x in ps["out"].values()
                            if x["loc"] is not None and not x["builtin"])):
            if l >= 1:
                problems["ps-out-no-attachment"] += 1
                if problems["ps-out-no-attachment"] <= 3:
                    print("draw#%-4d PS пишет Location %d (%s), а цветовое"
                          " приложение одно" % (d["n"], l, v["semantic"]))
    print("\nпара VS/PS уникальных: %d" % len(seen_pairs))
    for k, n in problems.most_common():
        print("  %-22s %d" % (k, n))
    if verbose:
        for sid in sorted(cache):
            i = cache[sid]
            if not i:
                continue
            print("\nшейдер %d %s (%s)" % (sid, shaders[sid]["entry"].decode(
                "ascii", "replace"), i["stage"]))
            for kind in ("in", "out"):
                for v in sorted(i[kind].values(),
                                key=lambda x: (x["loc"] if x["loc"] is not None
                                               else -1)):
                    print("   %-3s loc=%-3s %-12s %-6s n=%s%s" %
                          (kind, v["loc"], v["semantic"], v["type"], v["n"],
                           " BUILTIN " + v["builtin"] if v["builtin"] else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    check(a.file, a.verbose)


if __name__ == "__main__":
    main()
