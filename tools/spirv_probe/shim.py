#!/usr/bin/env python3
# shim.py — минимальная прослойка HLSL-SM3 -> HLSL-SM6 для DXC -> SPIR-V.
#
# Задача спайка — не «сделать правильно», а померить, сколько материалов
# компилируется в SPIR-V после механической подмены, а сколько требуют правки
# исходников. Поэтому правила грубые и полные только для тех конструкций,
# которые реально встречаются в Shaders/*.hlsl (замер §spirv_probe/README).
#
# Подмены:
#   sampler2D/samplerCUBE NAME : register(...)  -> Texture2D/TextureCube NAME
#                                                 + один общий SamplerState g_smp
#   register(cN)/register(sN)/register(bN)      -> снимаем (DXC сам раскладывает
#                                                 по cBuffers/texture/sampler)
#   tex2D/texCUBE/tex2Dlod/tex2Dproj            -> Sample/SampleLevel (+ proj вручную)
#   : POSITION (выход VS)                       -> : SV_POSITION
#   : COLOR    (выход PS)                       -> : SV_TARGET
#   VPOS / VFACE                                -> SV_Position / SV_IsFrontFace
import re

HEADER = """
// ---- pw spirv_probe shim ----
#ifndef PW_G_SMP
#define PW_G_SMP
SamplerState g_smp;
#endif
"""

SAMPLER_DECL = re.compile(
    r"\b(sampler2D|samplerCUBE|sampler|sampler_state|samplerSOM)\s+(\w+)\s*(?::\s*register\s*\([^)]*\))?\s*;")
REG_ANY = re.compile(r"\s*:\s*register\s*\([^)]*\)")

_TEXCOORD_CTR = [200]

def rewrite(src, stage=None, ps_ret_structs=()):
    notes = []
    _TEXCOORD_CTR[0] = 100   # счётчик — на файл, иначе семантика уезжает за лимит
    # 1. combined-сэмплеры -> ресурс; сам SamplerState один общий
    def sampler_sub(m):
        kind, name = m.group(1), m.group(2)
        if kind == "samplerCUBE":
            notes.append("samplerCUBE %s" % name)
            return "TextureCube %s;" % name
        if kind == "sampler_state":
            notes.append("sampler_state %s" % name)
            return "SamplerState %s;" % name
        notes.append("%s %s" % (kind, name))
        return "Texture2D %s;" % name
    src, n_samp = SAMPLER_DECL.subn(sampler_sub, src)

    # 2. register(...) во всех объявлениях — снимаем совсем
    src, n_reg = REG_ANY.subn("", src)

    # 3. выборки
    src, n_t2 = re.subn(r"\btex2D\s*\(\s*(\w+)\s*,", r"\1.Sample(g_smp,", src)
    src, n_cube = re.subn(r"\btexCUBE\s*\(\s*(\w+)\s*,", r"\1.Sample(g_smp,", src)
    # tex2Dlod( t, float4( uv2, 0, 0 ) ) -> t.SampleLevel(g_smp, uv2, 0)
    src, n_lod3 = re.subn(
        r"\btex2Dlod\s*\(\s*(\w+)\s*,\s*float4\s*\(\s*([^,()]+)\s*,\s*[^,()]+\s*,\s*[^,()]+\s*\)\s*\)",
        r"\1.SampleLevel(g_smp, \2, 0)", src)
    # tex2Dlod( t, VAR ) -> t.SampleLevel(g_smp, VAR.xy, VAR.w)  (общая форма)
    src, n_lod_var = re.subn(r"\btex2Dlod\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                             r"\1.SampleLevel(g_smp, \2.xy, \2.w)", src)
    # tex2Dlod( t, float4( u,v,0,mip ) ) -> t.SampleLevel(g_smp, float2(u,v), mip)
    def lod_sub(m):
        t, a, b, d = m.group(1), m.group(2), m.group(3), m.group(4)
        return "%s.SampleLevel(g_smp, float2(%s,%s), %s)" % (t, a, b, d)
    src, n_lod = re.subn(
        r"\btex2Dlod\s*\(\s*(\w+)\s*,\s*float4\s*\(\s*([^,()]+)\s*,\s*([^,()]+)\s*,\s*[^,()]+\s*,\s*([^,()]+)\s*\)\s*\)",
        lod_sub, src)
    # tex2Dproj( t, float4 ) -> t.Sample(g_smp, c.xy / c.w)
    def proj_sub(m):
        t, c = m.group(1), m.group(2)
        return "%s.Sample(g_smp, %s.xy / %s.w)" % (t, c, c)
    src, n_proj = re.subn(r"\btex2Dproj\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)", proj_sub, src)

    # 4. семантики вывода.
    #    POSITION на выходе VS -> SV_POSITION;
    #    COLOR на выходе PS    -> SV_TARGET, а в структурах (вход/выход) —
    #    в нейтральную TEXCOORDn: в SM6 COLOR в профиле ps недопустим.
    src, n_pos = re.subn(r":\s*POSITION\b", ": SV_POSITION", src)
    # семантика возврата самой точки входа (у материала их может быть несколько —
    # по одному на проход)
    if stage == "ps":
        src = re.sub(r"(\w+_PSMain\s*\([^)]*\))\s*:\s*COLOR\d*\b",
                     lambda m: m.group(1) + " : SV_TARGET", src)
    elif stage == "vs":
        src = re.sub(r"(\w+_VSMain\s*\([^)]*\))\s*:\s*COLOR\d*\b",
                     lambda m: m.group(1) + " : TEXCOORD190", src)
    # Структура, которой возвращает себя PS-точка входа, — это выходы-мишени:
    # её COLORn обязаны стать SV_TARGETn (в SM6 иначе «invalid usage of
    # semantic in shader profile ps»). Остальные COLOR — обычные пользовательские
    # семантики, уводим в незанятые TEXCOORDn.
    # Имена структур, которыми возвращают себя PS-точки входа, собираются по
    # всем материалам: сама структура может быть объявлена во включаемом файле.
    ret_structs = list(ps_ret_structs)
    if stage == "ps":
        m = re.search(r"^\s*(\w+)\s+\w+_PSMain\s*\(", src, re.M)
        if m and m.group(1) not in ("float4", "float3", "float", "half4", "half3", "half") \
           and m.group(1) not in ret_structs:
            ret_structs.append(m.group(1))
    for ret_struct in ret_structs:
        sm = re.search(r"struct\s+%s\s*\{(.*?)\n\s*\};" % ret_struct, src, re.S)
        if sm:
            body = sm.group(1)
            idx = [0]
            def to_target(m2):
                r = ": SV_TARGET%d" % idx[0]
                idx[0] += 1
                return r
            newbody = re.sub(r":\s*COLOR\d*\b", to_target, body)
            src = src[:sm.start(1)] + newbody + src[sm.end(1):]
    def color_field(m):
        _TEXCOORD_CTR[0] += 1
        return ": TEXCOORD%d" % _TEXCOORD_CTR[0]
    src, n_col = re.subn(r":\s*COLOR\d*\b", color_field, src)
    src, n_vpos = re.subn(r"\bVPOS\b", "SV_Position", src)
    # FragCoord по Vulkan-спеке — обязательно 4-компонентный; в исходниках
    # float2 PixelPos : VPOS, поэтому расширяем до float4 (использование .xy не меняется)
    src, n_frag = re.subn(r"\b(float2)\s+(\w+\s*:\s*SV_Position)", r"float4 \2", src)
    src, n_vface = re.subn(r"\bVFACE\b", "SV_IsFrontFace", src)

    stats = dict(samplers=n_samp, regs=n_reg, tex2D=n_t2, texCUBE=n_cube,
                 tex2Dlod=n_lod + n_lod_var + n_lod3, tex2Dproj=n_proj, position=n_pos,
                 color=n_col, vpos=n_vpos + n_frag, vface=n_vface)
    return HEADER + src, stats, notes

if __name__ == "__main__":
    import sys
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    out, st, notes = rewrite(src)
    sys.stdout.write(out)
    sys.stderr.write(str(st) + "\n")
