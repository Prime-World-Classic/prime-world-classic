#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Общий драйвер компиляции HLSL-корпуса в SPIR-V: пути, разбор MaterialSpec.xml,
# прослойка SM3->SM6, запуск DXC. Один источник для двух потребителей:
#   tools/shader_budget/shader_budget.py — бюджет дескрипторов по корпусу (§3.z в),
#   tools/shader_build/mkshd2.py         — писатель контейнера .shd v2 (шаг 11).
#
# Почему это вообще нужно: DXC нативный (Linux), а поставляемый SM3-DXBC делают
# fxc/D3DCompile на Windows-конуре. Два разных компилятора — два разных слоя в
# одном контейнере; сводить их «на глаз» нельзя, отсюда общий вход (тот же
# корпус, те же пины, та же прослойка).

import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

_HERE = os.path.dirname(os.path.abspath(__file__))
_PROBE = os.path.abspath(os.path.join(_HERE, "..", "spirv_probe"))
sys.path.insert(0, _PROBE)
import shim                                             # noqa: E402

R1117 = os.path.abspath(os.path.join(_HERE, "..", "..", "pw", "branches", "r1117"))
SH = os.path.join(R1117, "Src", "Render", "Shaders")
RENDER = os.path.join(R1117, "Src", "Render")
UBER = os.path.join(R1117, "Data", "Shaders", "UberShaders")
DXC = os.path.join(_PROBE, "vendor", "dxc", "bin", "dxc")
ENV = dict(os.environ, LD_LIBRARY_PATH=os.path.join(_PROBE, "vendor", "dxc", "lib"))

# Генератор поставляемых контейнеров жёстко берёт эти точки входа
# (Src/ShaderCompiler/ShaderCompiler.cpp:203-204), значит и вторая цель обязана
# брать их же — иначе в одном контейнере лежат разные варианты одного индекса.
ENTRY_VS = "MainPass_VSMain"
ENTRY_PS = "MainPass_PSMain"


def spec_materials():
    """[(имя, [(pinName, maxPinValue, pinMultiplier)])] в порядке XML.

    maxPinValue нужен, чтобы декодировать индекс пары в пины (проверка
    диапазона), а не только перемножать.
    """
    root = ET.parse(os.path.join(SH, "MaterialSpec.xml")).getroot()
    out = []
    for item in root.iter("Item"):
        name = item.findtext("shaderName")
        if not name:
            continue
        pins = [(p.findtext("pinName"), int(p.findtext("maxPinValue", "1")),
                 int(p.findtext("pinMultiplier", "1")))
                for p in item.iter("Item") if p.findtext("pinName")]
        out.append((name, pins))
    return out


def total_pairs(pins):
    """Пространство индексов материала = maxPinValue * pinMultiplier последнего пина."""
    if not pins:
        return 1
    last = pins[-1]
    return last[1] * last[2]


def decode_index(index, pins):
    """Индекс пары -> [(pinName, значение)] или None, если индекс вне пространства.

    Декод повторяет генератор (Src/ShaderCompiler/ShaderCompiler.cpp:186-201):
    обход пинов с конца, value = idx / mult, idx %= mult.
    """
    out = []
    rest = index
    for name, mx, mult in reversed(pins):
        value = rest // mult
        rest %= mult
        if value >= mx:
            return None
        out.append((name, value))
    out.reverse()
    if rest:
        return None
    return out


class ShimTree:
    """Сгенерированное дерево шейдеров SM3->SM6.

    Живёт ВНУТРИ Src/Render (иначе DXC не находит stdafx.h и относительные
    include), поэтому обязано удаляться по завершении — в рабочем дереве
    репозитория его быть не должно (в .gitignore правило
    `pw/branches/r1117/Src/Render/zzz_probe_shaders_*/`).
    """

    def __init__(self, prefix="zzz_build_shaders_", set_layout=False, cbank=False):
        self.prefix = prefix
        self.set_layout = set_layout
        self.cbank = cbank
        self.bank_members = []
        self.dir = None
        self.src = None

    def _struct_names(self):
        names_ps, names_vs = set(), set()
        for fn in os.listdir(SH):
            if not fn.endswith(".hlsl"):
                continue
            t = open(os.path.join(SH, fn), encoding="utf-8", errors="replace").read()
            for m in re.finditer(r"^\s*(\w+)\s+\w+_PSMain\s*\(", t, re.M):
                names_ps.add(m.group(1))
            for m in re.finditer(r"^\s*(\w+)\s+\w+_VSMain\s*\(", t, re.M):
                names_vs.add(m.group(1))
        return tuple(sorted(names_ps)), tuple(sorted(names_vs))

    def build(self, stage, material=None, defines=()):
        """Дерево под один прогон DXC.

        `material` + `defines` нужны режиму `cbank`: банк строится по
        ФАКТИЧЕСКИ дошедшим объявлениям этой пары (пины меняют набор членов),
        поэтому он общий для всей пары VS+PS, но не для материала целиком.
        """
        names_ps, names_vs = self._struct_names()
        self.cleanup()
        self.dir = os.path.join(RENDER, self.prefix + stage)
        # Зеркало на уровень глубже: в исходниках есть `#include
        # "..\OnlineShaders\_commonDepthRead.h"` и `#include "../GlobalMasks.h"`,
        # поэтому `..` от файла дерева обязан попадать в зеркало, а не в настоящий
        # Src/Render (иначе под дерево подпадают НАСТОЯЩИЕ OnlineShaders с
        # неснятыми c-объявлениями → «redefinition of 'madValueUV'»)
        self.src = os.path.join(self.dir, "src")
        os.makedirs(self.src, exist_ok=True)
        bank_txt = None
        if self.cbank and material:
            import cbank as cb
            entry = os.path.join(SH, material + ".hlsl")
            text = cb.preprocess(entry, defines)
            bank_txt, members, _dead, _drop = cb.bank_block(
                entry, cb.mask_map(), stage, text=text)
            self.bank_members = [(m[0], m[1], m[2]) for m in members]
        for fn in os.listdir(SH):
            src = os.path.join(SH, fn)
            if not os.path.isfile(src):
                continue
            if fn.endswith(".hlsl"):
                out, _st, _n = shim.rewrite(
                    open(src, encoding="utf-8", errors="replace").read(), stage,
                    names_ps, names_vs, set_layout=self.set_layout,
                    cbank=self.cbank)
                if bank_txt is not None and fn == material + ".hlsl":
                    # банк — ОТДЕЛЬНЫМ файлом и включается ПЕРВЫМ: члены должны
                    # быть видны функциям из `#include "_common.hlsl"`, а не
                    # только телу точки входа
                    open(os.path.join(self.src, "pw_cbank_%s.hlsl" % stage),
                         "w", encoding="utf-8").write(bank_txt + "\n")
                    out = '#include "pw_cbank_%s.hlsl"\n' % stage + out
                open(os.path.join(self.src, fn), "w", encoding="utf-8").write(out)
            elif fn.endswith(".h"):
                open(os.path.join(self.src, fn), "wb").write(open(src, "rb").read())
        # заголовки вне Shaders/, доходящие через `../`
        gm = os.path.join(RENDER, "GlobalMasks.h")
        if os.path.isfile(gm):
            shutil.copyfile(gm, os.path.join(self.dir, "GlobalMasks.h"))
        osh = os.path.join(RENDER, "OnlineShaders")
        if os.path.isdir(osh):
            d2 = os.path.join(self.dir, "OnlineShaders")
            os.makedirs(d2, exist_ok=True)
            for fn in sorted(os.listdir(osh)):
                if not fn.endswith((".h", ".hlsl")):
                    continue
                out, _st, _n = shim.rewrite(
                    open(os.path.join(osh, fn), encoding="utf-8",
                         errors="replace").read(), stage,
                    names_ps, names_vs, set_layout=self.set_layout,
                    cbank=self.cbank)
                open(os.path.join(d2, fn), "w", encoding="utf-8").write(out)
        return self.dir

    def cleanup(self):
        if os.path.isdir(RENDER):
            for fn in os.listdir(RENDER):
                d = os.path.join(RENDER, fn)
                if fn.startswith(self.prefix) and os.path.isdir(d):
                    shutil.rmtree(d, ignore_errors=True)
        self.dir = None


def compile_dxc(path, stage, entry, defines, tree=None, out_spv=None,
                extra_args=("-Zpr",)):
    """Один прогон DXC. stage: 'vs' | 'ps'. defines: ['NAME=VAL', ...].

    -Zpr обязателен: исходники пишутся под row-major (D3D9), а DXC по умолчанию
    компилирует mul(v, M) как column-major — теряется перенос матрицы (замер
    спайка B0). entry задаётся явно: DXC не знает имени точки входа из SM3-кода.
    """
    if tree is not None and tree.dir:
        base = getattr(tree, "src", None) or tree.dir
        path = os.path.join(base, os.path.basename(path))
        inc = base
    else:
        inc = SH
    if out_spv is None:
        out_spv = "/tmp/dxcdrv_%s_%s.spv" % (os.path.basename(path), stage)
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv"] + list(extra_args) + \
          ["-I", inc, "-I", RENDER]
    for d in defines:
        cmd += ["-D", d]
    cmd += [path, "-Fo", out_spv]
    p = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    return p.returncode, (p.stdout or "") + (p.stderr or ""), out_spv
