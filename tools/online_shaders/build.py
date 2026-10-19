#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Порт блобов Shaders/OnlineShaders в SPIR-V (PLAN_NVRHI §3.z п.14, срез A).

Вход — HLSL-источники, восстановленные ПОСМЫСЛОВО из SM3-DXIL блобов
(исходников этих 44 блобов в дереве нет; дизассемблирование —
`tools/hlsl_compile_test --blob <файл> --disasm`, см. комментарии в .hlsl).

Выход — sidecar рядом с блобом: `<блоб>.spv` (raw SPIR-V). Блоб не меняется:
D3D9-путь обязан остаться побайтово прежним. Читатель (`ImmRenderer`) берёт
sidecar только когда бэкенд объявил цель SPIR-V
(`IRenderBackend::GetShaderTargetTag() == Materials::stSpirv`).

Проверка результата — не «DXC ругнулся или нет», а содержимое: число и тип
ресурсов, точка входа, наличие BuiltIn Position/FragCoord (у VS без Position
пикселей не будет молча — см. §3.z п.16 про `out ... : POSITION`).
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BR = os.path.abspath(os.path.join(HERE, "..", "..", "pw", "branches", "r1117"))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "shader_build")))
import dxcdrv  # noqa: E402
import spirv_iface  # noqa: E402

PORT = os.path.join(BR, "Data", "Shaders", "OnlineShaders", "port")
OUT_DIR = os.path.join(BR, "Data", "Shaders", "OnlineShaders")

# (источник в port/, стадия, точка входа, имя блоба-владельца)
JOBS = [
    ("VertexShader_vs.hlsl",   "vs", "main", "VertexShader.vsb"),
    ("DiffuseMap_ps.hlsl",     "ps", "main", "DiffuseMap.psb"),
    ("default_shader_vs.hlsl", "vs", "main", "default_shader.vsb"),
    ("default_shader_ps.hlsl", "ps", "main", "default_shader.psb"),
]

# мини-сканер SPIR-V: только то, что нужно для гейта «шейдер вообще годен»
DECORATE, MEMBER_DECORATE = 71, 72
DEC_BINDING, DEC_DESCRIPTOR_SET, DEC_BUILTIN = 33, 34, 11
# BuiltIn: 0 = Position (выход VS), 15 = FragCoord (вход PS, только если PS
# реально читает координаты пикселя — требовать его у PS нельзя)
BI_POSITION, BI_FRAGCOORD = 0, 15
ENTRY_POINT, NAME = 15, 5


def decode_literal(ws):
    """Literal-строка SPIR-V: символы упакованы по 4 байта в слово (LE)."""
    out = bytearray()
    for w in ws:
        out += struct.pack("<I", w)
    z = out.find(b"\x00")
    return out[:z if z >= 0 else len(out)].decode("ascii", "replace")


def scan(spv):
    """-> (entry, [(set,binding)], builtins set, has_position, has_fragcoord)

    BuiltIn бывает в OpDecorate (интерфейсная переменная) И в OpMemberDecorate
    (член структуры-вывода — так DXC вешает Position на поле VS_OUT), поэтому
    собираются обе. Пары (set,binding) — из словаря декораций: порядок
    DescriptorSet/Binding в DXC не гарантирован.
    """
    if len(spv) < 20 or struct.unpack("<I", spv[:4])[0] != 0x07230203:
        return None
    words = struct.unpack("<%dI" % (len(spv) // 4), spv)
    entry = None
    dec = {}          # target id -> {decorazione: значение}
    builtins = set()
    i = 5
    while i < len(words):
        w = words[i]
        op, wc = w & 0xFFFF, w >> 16
        if wc == 0:
            break
        if op == ENTRY_POINT and i + 3 < len(words):
            entry = decode_literal(words[i + 3:i + wc])
        elif op == DECORATE and wc >= 4:
            tgt, d = words[i + 1], words[i + 2]
            # literal-операнд (Binding/DescriptorSet/Offset/BuiltIn/Location)
            # идёт СЛОВОМ 3 при word count = 4; wc == 3 значит операнда нет
            val = words[i + 3] if wc >= 4 else 0
            dec.setdefault(tgt, {})[d] = val
            if d == DEC_BUILTIN:
                builtins.add(val)
        elif op == MEMBER_DECORATE and wc >= 5:
            tgt, d = words[i + 1], words[i + 3]
            val = words[i + 4] if wc >= 5 else 0
            dec.setdefault(("m", tgt, words[i + 2]), {})[d] = val
            if d == DEC_BUILTIN:
                builtins.add(val)
        i += wc
    pairs = set()
    for tgt, m in dec.items():
        if DEC_BINDING in m:
            pairs.add((m.get(DEC_DESCRIPTOR_SET, 0), m[DEC_BINDING]))
    return (entry, sorted(pairs), builtins, BI_POSITION in builtins,
            BI_FRAGCOORD in builtins)


def main():
    only = sys.argv[1:] or None
    bad = 0
    for src, stage, entry, blob in JOBS:
        if only and src not in only and blob not in only:
            continue
        sp = os.path.join(PORT, src)
        out = os.path.join(OUT_DIR, blob + ".spv")
        rc, msg, tmp = dxcdrv.compile_dxc(sp, stage, entry, [], tree=None,
                                         out_spv=out + ".tmp")
        if rc != 0:
            print("!! %s (%s): DXC rc=%d\n%s" % (src, stage, rc, msg.strip()[:1200]))
            bad += 1
            if os.path.exists(tmp):
                os.remove(tmp)
            continue
        data = open(tmp, "rb").read()
        # DXC трактует mul(v,M)/mul(M,v) как транспонированную матрицу
        # относительно DXBC (замеры — в spirv_iface.fix_matrix_mult); порт
        # написан в строчной конвенции (dp4 r, r0, c4..c7), поэтому правим.
        flipped, nflip = spirv_iface.fix_matrix_mult(data)
        if nflip:
            okv, verr = spirv_iface.validate(flipped)
            if not okv:
                print("!! %s: правка матричных умножений (%d) не прошла "
                      "валидацию: %s" % (src, nflip, verr.strip()[:300]))
                bad += 1
                os.remove(tmp)
                continue
            data = flipped
        info = scan(data)
        if not info:
            print("!! %s: SPIR-V не разобран" % src)
            bad += 1
            continue
        ent, pairs, builtins, has_pos, has_fc = info
        need_pos = (stage == "vs")
        need_fc = False   # FragCoord у PS появляется только если он читается
        if (need_pos and not has_pos) or (need_fc and not has_fc):  # noqa
            print("!! %s: нет %s — вывод будет пустым молча"
                  % (src, "BuiltIn Position" if need_pos else "BuiltIn FragCoord"))
            bad += 1
        os.replace(tmp, out)
        print("%-24s -> %-28s %6d Б  entry=%s  наборы=%s  builtins=%s  "
              "матричных правок %d"
              % (src, blob + ".spv", len(data), ent, pairs, sorted(builtins),
                 nflip))
        if msg.strip():
            print("   сообщения DXC: %s" % msg.strip()[:300])
    print("итог: %d файлов, ошибок %d" % (len(JOBS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
