#!/usr/bin/env python3
# gen_b0_shader.py — сгенерировать SPIR-V для b0_probe.hlsl и заголовок с
# байтами для nvrhi_probe.
#
# Делает ровно то же, что боевой путь для Vulkan: прослойка shim.py (диалект
# клиента -> HLSL SM6) -> DXC -> SPIR-V. Заодно печатает РАСКЛАДКУ, которую
# выбрал DXC (binding'и uniform-буфера/текстуры/сэмплера и location'ы вершинных
# атрибутов) — по ней же и пишется код пробы.
#
# Запуск: ./gen_b0_shader.py        (из каталога tools/spirv_probe)
import os, subprocess, sys
import shim

HERE = os.path.dirname(os.path.abspath(__file__))
SH = os.path.join(HERE, "shaders")
DXC = os.path.join(HERE, "vendor", "dxc", "bin", "dxc")
ENV = dict(os.environ, LD_LIBRARY_PATH=os.path.join(HERE, "vendor", "dxc", "lib"))
SRC = os.path.join(SH, "b0_probe.hlsl")

VS_RET = ("VS_OUT",)
PS_RET = ()

TRIVIAL = os.path.join(SH, "b0_trivial.hlsl")

# -Zpr: DXC по умолчанию раскладывает float4x4 для SPIR-V СТОЛБЦАМИ, из-за чего
# матрицы клиента (D3D9/HLSL: строки, перенос в последней строке) читаются
# транспонированными — mul(v, M) теряет перенос. С -Zpr память матрицы остаётся
# строчной, как в D3D9, и mul(v, M) совпадает с D3D9 (замерено: без -Zpr кадр
# уезжает в центр, с -Zpr — пиксель в пиксель с CPU-эталоном).
EXTRA = (["-Zpr"] + os.environ.get("PW_DXC_EXTRA", "").split())


def compile_trivial(stage):
    """Чистый SM6: без прослойки, имя точки входа — своё."""
    entry = "trivial_%s" % stage
    spv = os.path.join(SH, "zzz_triv_%s.spv" % stage)
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv", "-Od"] + EXTRA + ["-I", SH, TRIVIAL, "-Fo", spv]
    p = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit("DXC trivial %s: %s" % (stage, (p.stdout + p.stderr)[-800:]))
    data = open(spv, "rb").read()
    os.unlink(spv)
    return data


BARE = os.path.join(SH, "b0_bare.hlsl")

def compile_bare(stage):
    entry = "bare_%s" % stage
    spv = os.path.join(SH, "zzz_bare_%s.spv" % stage)
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv", "-Od"] + EXTRA + ["-I", SH, BARE, "-Fo", spv]
    r = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("DXC bare %s: %s" % (stage, (r.stdout + r.stderr)[-800:]))
    data = open(spv, "rb").read()
    os.unlink(spv)
    return data


VB = os.path.join(SH, "b0_vb.hlsl")

def compile_vb(stage):
    entry = "vb_%s" % stage
    spv = os.path.join(SH, "zzz_vb_%s.spv" % stage)
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv", "-Od"] + EXTRA + ["-I", SH, VB, "-Fo", spv]
    r = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("DXC vb %s: %s" % (stage, (r.stdout + r.stderr)[-800:]))
    data = open(spv, "rb").read()
    os.unlink(spv)
    return data


def compile_stage(stage):
    src = open(SRC, encoding="utf-8", errors="replace").read()
    out, stats, _ = shim.rewrite(src, stage, PS_RET, VS_RET)
    tmp = os.path.join(SH, "zzz_b0_%s.hlsl" % stage)
    open(tmp, "w", encoding="utf-8").write(out)
    spv = os.path.join(SH, "zzz_b0_%s.spv" % stage)
    entry = "MainPass_%sMain" % stage.upper()
    cmd = [DXC, "-T", stage + "_6_0", "-E", entry, "-spirv", "-Od"] + EXTRA + ["-Vd",
           "-I", SH, tmp, "-Fo", spv]
    p = subprocess.run(cmd, env=ENV, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(out)
        raise SystemExit("DXC %s: %s" % (stage, (p.stdout + p.stderr)[-1500:]))
    data = open(spv, "rb").read()
    os.unlink(tmp)
    os.unlink(spv)
    return data, stats

# ------------------------------------------------- разбор SPIR-V (раскладка)
OP = {5: "OpName", 77: "OpMemberName", 71: "OpDecorate", 72: "OpMemberDecorate",
      30: "OpTypeStruct", 32: "OpTypePointer", 59: "OpVariable"}
DEC = {30: "Location", 33: "Binding", 34: "DescriptorSet", 35: "Offset",
       43: "ArrayStride", 36: "MatrixStride", 19: "ColMajor", 20: "RowMajor"}
STORAGE = {0: "UniformConstant", 1: "Input", 2: "Uniform", 3: "StorageBuffer",
           6: "Output", 9: "Function", 8: "Private"}

def parse_spv(data):
    import struct
    if struct.unpack_from("<I", data, 0)[0] != 0x07230203:
        raise SystemExit("не SPIR-V")
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    i, names, memnames, decs, vars_, ptrs = 5, {}, {}, {}, [], {}
    while i < len(words):
        w = words[i]
        op, wc = w & 0xFFFF, w >> 16
        if wc == 0:
            break
        args = words[i + 1:i + wc]
        if op == 5 and args:
            raw = b"".join(struct.pack("<I", w) for w in args[1:])
            names[args[0]] = raw.split(b"\0")[0].decode("utf-8", "replace")
        elif op == 77 and len(args) > 2:
            raw = b"".join(struct.pack("<I", w) for w in args[2:])
            memnames[(args[0], args[1])] = raw.split(b"\0")[0].decode("utf-8", "replace")
        elif op == 71 and len(args) >= 2:
            decs.setdefault(args[0], []).append((DEC.get(args[1], args[1]),
                                                 args[2] if len(args) > 2 else None))
        elif op == 32 and len(args) >= 3:
            ptrs[args[0]] = (args[1], args[2])          # result -> (storage, type)
        elif op == 59 and len(args) >= 3:
            vars_.append((args[2], args[1], args[0]))   # storage, result, ptr type
        i += wc
    out = []
    for storage, res, ptr in vars_:
        st, target = ptrs.get(ptr, (None, None))
        d = list(decs.get(res, [])) + list(decs.get(target, []))
        out.append(dict(storage=STORAGE.get(storage, storage), name=names.get(res, "?"),
                        type_id=target, decorations=d))
    return out

def emit_header(vs, ps, tvs, tps, bvs, bps, vvs, vps_, path):
    def arr(name, data):
        s = ["static const unsigned char %s[] = {" % name]
        for i in range(0, len(data), 16):
            s.append("  " + ",".join("%d" % b for b in data[i:i + 16]) + ",")
        s.append("};")
        s.append("static const unsigned int %s_size = %d;" % (name, len(data)))
        return "\n".join(s)
    open(path, "w", encoding="utf-8").write(
        "// сгенерировано tools/spirv_probe/gen_b0_shader.py — не править вручную\n"
        "#pragma once\n\n" + arr("g_b0VsSpv", vs) + "\n\n" + arr("g_b0PsSpv", ps) +
        "\n\n// чистый SM6 (без прослойки) — для отделения перевода от вызова NVRHI\n" +
        arr("g_b0TVsSpv", tvs) + "\n\n" + arr("g_b0TPsSpv", tps) +
        "\n\n// без ресурсов и без вершинных входов\n" +
        arr("g_b0BareVsSpv", bvs) + "\n\n" + arr("g_b0BarePsSpv", bps) +
        "\n\n// вершинные данные без ресурсов\n" +
        arr("g_b0VbVsSpv", vvs) + "\n\n" + arr("g_b0VbPsSpv", vps_) + "\n")

def main():
    vs, st_vs = compile_stage("vs")
    ps, st_ps = compile_stage("ps")
    print("прослойка VS:", st_vs)
    print("прослойка PS:", st_ps)
    print("размер SPIR-V: VS=%d байт, PS=%d байт" % (len(vs), len(ps)))
    for tag, data in (("VS", vs), ("PS", ps)):
        print("\nраскладка %s:" % tag)
        for v in parse_spv(data):
            print("  %-16s %-14s type=%-4s %s" % (v["storage"], v["name"], v["type_id"],
                                                  v["decorations"]))
    vvs, vps_ = compile_vb("vs"), compile_vb("ps")
    bvs, bps = compile_bare("vs"), compile_bare("ps")
    tvs, tps = compile_trivial("vs"), compile_trivial("ps")
    print("тривиальный SM6: VS=%d, PS=%d байт" % (len(tvs), len(tps)))
    hdr = os.path.join(HERE, "..", "nvrhi_probe", "b0_shader_spv.h")
    emit_header(vs, ps, tvs, tps, bvs, bps, vvs, vps_, hdr)
    print("\nзаголовок: %s" % os.path.relpath(hdr, os.path.join(HERE, "..", "..")))

if __name__ == "__main__":
    main()
