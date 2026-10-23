#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сборка SPIR-V sidecar-ей для ВСЕХ блобов Shaders/OnlineShaders.

Зачем. Блобы `*.psb`/`*.vsb` — SM3-DXIL; Vulkan-бэкенд умеет только SPIR-V,
поэтому без sidecar-я каждый пост-эффект падает (`Creating pixel shader
error! D3DERR_INVALIDCALL`, `Вывод: нет PS`). Замер прогона 79: 30374 таких
сбоя, 35201 отказа (Convolve7 7208, WarfogBlend/NatureMap/DecodeZ 3605,
WaterHMap/WaterHDXDYMap/ApplyAlpha2 3604, Convolve9 1467) — из-за этого нет
миникарты, bloom/warfog, водяных карт.

Раньше (срез A, `build.py`) эти шейдеры портили ВРУЧНУЮ по дизассемблированию
SM3 — исходников не нашли. Они нашлись: `Src/Render/OnlineShaders/*.psh` (PS),
`*.pss` (PS с /Gfa), `*.vsh` (VS), компилируются `Vendor/BuildUtils/
CompileOnlineShaders.cmd` (`/T ps_3_0 /E PS_main`, `/T vs_3_0 /E VS_main`).
Значит sidecar-и собираются тем же конвейером, что и корпус UberShaders:
прослойка SM3->SM6 (`tools/spirv_probe/shim.py`) + канонический c-банк
(`tools/shader_build/cbank.py`) + DXC + правка транспонирования матриц
(`spirv_iface.fix_matrix_mult`).

Точку входа переименовываем в `MainPass_PSMain`/`MainPass_VSMain`: прослойка и
cbank ищут точку входа по суффиксу `_PSMain`/`_VSMain` (в корпусе материалов так
и называют), а шеву имя не важно — он берёт его из OpEntryPoint (`SpvScan`).

Выход — `<блоб>.spv` рядом с блобом в `Data/Shaders/OnlineShaders/`; сам блоб
не меняется (D3D9-путь обязан остаться побайтово прежним).
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BR = os.path.abspath(os.path.join(HERE, "..", "..", "pw", "branches", "r1117"))
RENDER = os.path.join(BR, "Src", "Render")
OSRC = os.path.join(RENDER, "OnlineShaders")
SHAD = os.path.join(RENDER, "Shaders")
OUT_DIR = os.path.join(BR, "Data", "Shaders", "OnlineShaders")

sys.path.insert(0, os.path.join(HERE, "..", "shader_build"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "spirv_probe"))
import cbank        # noqa: E402
import dxcdrv       # noqa: E402
import shim         # noqa: E402
import spirv_iface  # noqa: E402

# дерево-зеркало внутри Src/Render (иначе DXC не находит GlobalMasks.h по `../`);
# префикс zzz_probe_shaders_ — под правилом .gitignore, дерево обязано удаляться
TREE_RAW = os.path.join(RENDER, "zzz_probe_shaders_osh_raw")
TREE_SHIM = os.path.join(RENDER, "zzz_probe_shaders_osh_shim")

STAGE_OF_EXT = {".psh": "ps", ".pss": "ps", ".vsh": "vs"}
ENTRY_OF_STAGE = {"ps": "PS_main", "vs": "VS_main"}
RENAMED_OF_STAGE = {"ps": "MainPass_PSMain", "vs": "MainPass_VSMain"}
BLOB_EXT_OF_STAGE = {"ps": ".psb", "vs": ".vsb"}


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def rename_entry(text, stage):
    """PS_main -> MainPass_PSMain (и VS_main -> MainPass_VSMain) везде."""
    old, new = ENTRY_OF_STAGE[stage], RENAMED_OF_STAGE[stage]
    return text.replace(old, new)


def fix_includes(text):
    """`#include "X.psh"` -> `#include "X_psh.hlsl"`.

    DXC не знает расширений .psh/.pss/.vsh, а `default_shader.psh` включает
    `default_shader.vsh`: без суффикса стадии оба файла зеркалились бы одним
    именем (самоinclude). Поэтому имя файла-зеркала = `<база>_<расширение>.hlsl`.
    """
    def sub(m):
        base, ext = m.group(1), m.group(2)   # ext без точки: psh / pss / vsh
        return '#include "%s_%s.hlsl"' % (os.path.basename(base), ext)
    return re.sub(r'#\s*include\s*"([^"\\]*?)\.(psh|pss|vsh)("|>)', sub, text)


def mirror(dirs, stage, rewritten):
    """Зеркало исходников в дерево. rewritten=True — прогонять через shim.rewrite.

    `dirs` — список каталогов-источников. Файлы .psh/.pss/.vsh пишутся как
    .hlsl (DXC не знает других расширений), с переименованной точкой входа.
    """
    for src_dir in dirs:
        for fn in sorted(os.listdir(src_dir)):
            p = os.path.join(src_dir, fn)
            if not os.path.isfile(p):
                continue
            ext = os.path.splitext(fn)[1]
            if ext not in (".h", ".hlsl", ".psh", ".pss", ".vsh"):
                continue
            out_name = fn
            if ext in STAGE_OF_EXT:
                out_name = fn[:-len(ext)] + "_" + ext[1:] + ".hlsl"
            dst_dir = os.path.join(TREE_SHIM if rewritten else TREE_RAW,
                                   os.path.basename(src_dir))
            os.makedirs(dst_dir, exist_ok=True)
            dst = os.path.join(dst_dir, out_name)
            text = fix_includes(rename_entry(read(p), stage))
            if rewritten:
                text, _st, _n = shim.rewrite(text, stage, set_layout=True,
                                             cbank=True, src_path=p)
            with open(dst, "w", encoding="utf-8") as f:
                f.write(text)


def build_tree(stage):
    for d in (TREE_RAW, TREE_SHIM):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)
    shutil.copyfile(os.path.join(RENDER, "GlobalMasks.h"),
                    os.path.join(TREE_RAW, "GlobalMasks.h"))
    shutil.copyfile(os.path.join(RENDER, "GlobalMasks.h"),
                    os.path.join(TREE_SHIM, "GlobalMasks.h"))
    mirror([OSRC, SHAD], stage, False)
    mirror([OSRC, SHAD], stage, True)


def cleanup():
    for d in (TREE_RAW, TREE_SHIM):
        shutil.rmtree(d, ignore_errors=True)


def compile_one(entry_path, stage, entry, incs, out_spv, defines=()):
    cmd = [dxcdrv.DXC, "-T", stage + "_6_0", "-E", entry, "-spirv", "-Zpr"]
    for i in incs:
        cmd += ["-I", i]
    for d in defines:
        cmd += ["-D", d]
    cmd += [entry_path, "-Fo", out_spv]
    p = subprocess.run(cmd, env=dxcdrv.ENV, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def main():
    ap = sys.argv[1:]
    only = None
    if "--only" in ap:
        only = ap[ap.index("--only") + 1].split(",")
    verbose = "-v" in ap

    sources = [fn for fn in sorted(os.listdir(OSRC))
               if os.path.splitext(fn)[1] in STAGE_OF_EXT]
    if only:
        sources = [fn for fn in sources
                   if any(s in fn for s in only)]

    built, failed = 0, []
    for stage in ("ps", "vs"):
        sel = [fn for fn in sources if STAGE_OF_EXT[os.path.splitext(fn)[1]] == stage]
        if not sel:
            continue
        build_tree(stage)
        for fn in sel:
            base = os.path.splitext(fn)[0]
            raw_entry = os.path.join(TREE_RAW, "OnlineShaders", base + "_" +
                                     os.path.splitext(fn)[1][1:] + ".hlsl")
            shim_entry = os.path.join(TREE_SHIM, "OnlineShaders", base + "_" +
                                      os.path.splitext(fn)[1][1:] + ".hlsl")
            entry = RENAMED_OF_STAGE[stage]
            blob = base + BLOB_EXT_OF_STAGE[stage]
            out_spv = "/tmp/osh_%s.spv" % base
            log = []
            # 1. канонический c-банк — по НЕПРЕОБРАЗОВАННОМУ дереву (как в
            #    dxcdrv.ShimTree: банк строится по исходным объявлениям)
            bank_txt = ""
            try:
                text = cbank.preprocess(raw_entry)
                bank_txt, members, dead, dropped = cbank.bank_block(
                    raw_entry, cbank.mask_map(), stage, text=text)
                log.append("банк: членов %d (мёртвых %d, снято %d)"
                           % (len(members), len(dead), len(dropped)))
            except SystemExit as e:
                failed.append((blob, "cbank: %s" % str(e)[:900]))
                print("FAIL %-28s %s" % (blob, str(e)[:900]))
                continue
            # банк — ПЕРВЫм include'ом, иначе члены не видны из _common*.h.
            # Копию пишем ОТДЕЛЬНЫМ файлом: `ApplyAlpha2.pss` включает
            # `ApplyAlpha.psh`, и правка файла в дереве ломала бы чужие
            # компиляции (двойное объявление c-буфера — замер 2026-10-18).
            banked = shim_entry[:-len(".hlsl")] + "_banked.hlsl"
            with open(shim_entry, "r", encoding="utf-8") as f:
                body = f.read()
            with open(os.path.join(TREE_SHIM, "OnlineShaders",
                                   "pw_cbank_%s.hlsl" % stage), "w",
                      encoding="utf-8") as f:
                f.write(bank_txt + "\n")
            with open(banked, "w", encoding="utf-8") as f:
                f.write('#include "pw_cbank_%s.hlsl"\n' % stage + body)
            # 2. DXC
            incs = [os.path.join(TREE_SHIM, "OnlineShaders"),
                    os.path.join(TREE_SHIM, "Shaders"), TREE_SHIM]
            rc, err = compile_one(banked, stage, entry, incs, out_spv)
            if rc != 0 or not os.path.isfile(out_spv):
                failed.append((blob, "dxc: %s" % err.strip()[-900:]))
                print("FAIL %-28s DXC: %s" % (blob, err.strip()[-900:]))
                continue
            with open(out_spv, "rb") as f:
                spv = f.read()
            # 3. транспонирование матриц (общая правка корпуса) + валидация
            patched, nflip = spirv_iface.fix_matrix_mult(spv)
            ok, verr = spirv_iface.validate(patched)
            if not ok:
                failed.append((blob, "spirv-val: %s" % verr[:200]))
                print("FAIL %-28s spirv-val: %s" % (blob, verr[:200]))
                continue
            dst = os.path.join(OUT_DIR, blob + ".spv")
            with open(dst, "wb") as f:
                f.write(patched)
            built += 1
            print("OK   %-28s %7d Б  матричных правок %d  %s"
                  % (blob + ".spv", len(patched), nflip, "; ".join(log)))
            if verbose:
                print("     ", err.strip()[:400])
    cleanup()
    print("ИТОГ: sidecar-ей %d, провалов %d" % (built, len(failed)))
    for blob, why in failed:
        print("  FAIL %s: %s" % (blob, why[:200]))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
