#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Генерирует Src/Render/stretch_shaders_spv.h из stretch_vs.hlsl/stretch_ps.hlsl.

Внутренний блит seam'а (StretchRect со скейлом) не может брать шейдеры у
клиента — им нужен собственный микс: clip space из вершинного буфера и одна
выборка из текстуры. SPIR-V вшивается в бинарник массивом байтов, чтобы путь
не зависел от пайла и от наличия sidecar-ей.

Компиляция: DXC тем же драйвером, что и корпус (tools/spirv_probe/vendor/dxc).
Ключи: -spirv -Zpr (row-major, как в остальном шейдерном хозяйстве seam'а).

Запуск:  python3 tools/shader_build/stretch/gen_header.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))          # .../tools
REPO = os.path.abspath(os.path.join(ROOT, ".."))                # корень worktree
DXC = os.path.join(REPO, "tools", "spirv_probe", "vendor", "dxc", "bin", "dxc")
OUT = os.path.join(REPO, "pw", "branches", "r1117", "Src", "Render",
                   "stretch_shaders_spv.h")

UNITS = [("stretch_vs.hlsl", "vs_6_0", "kStretchVsSpv"),
         ("stretch_ps.hlsl", "ps_6_0", "kStretchPsSpv")]


def compile_spv(src, stage):
    spv = os.path.join(HERE, os.path.splitext(src)[0] + ".spv")
    r = subprocess.run([DXC, "-T", stage, "-E", "main", "-spirv", "-Zpr",
                        os.path.join(HERE, src), "-Fo", spv],
                       capture_output=True)
    if r.returncode:
        sys.exit("%s: %s" % (src, (r.stdout + r.stderr).decode("utf-8", "replace")))
    with open(spv, "rb") as f:
        return f.read()


def main():
    parts = ["// ФАЙЛ СОЗДАН АВТОМАТИЧЕСКИ: tools/shader_build/stretch/gen_header.py",
             "// Источники — tools/shader_build/stretch/stretch_{vs,ps}.hlsl, компилятор — DXC",
             "// (-T <стадия>_6_0 -E main -spirv -Zpr). Не править руками: пересобрать скриптом.",
             "#ifndef STRETCH_SHADERS_SPV_H",
             "#define STRETCH_SHADERS_SPV_H",
             "",
             "// Внутренний блит со скейлом (StretchRect): VS отдаёт позицию из VB (clip space),",
             "// PS делает одну выборку. Набор 0 — текстура (binding 0), набор 1 — сэмплер (binding 0).",
             "// Входы VS: POSITION0 -> location 0 (RG32_FLOAT), TEXCOORD0 -> location 1 (RG32_FLOAT)."]
    for src, stage, name in UNITS:
        blob = compile_spv(src, stage)
        rows = []
        for i in range(0, len(blob), 16):
            rows.append("  " + ",".join("%d" % b for b in blob[i:i + 16]))
        parts.append("")
        parts.append("static const unsigned char %s[] = {" % name)
        parts.append(",\n".join(rows))
        parts.append("};")
        parts.append("static const unsigned int %sSize = %du;" % (name, len(blob)))
    parts.append("")
    parts.append("#endif // STRETCH_SHADERS_SPV_H")
    with open(OUT, "wb") as f:
        f.write(("\n".join(parts) + "\n").encode("utf-8"))
    print("записан %s" % OUT)


if __name__ == "__main__":
    main()
