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
#   register( МАКРОС )                          -> register( sN ) (GlobalMasks.h)
import os
import re

HEADER = """
// ---- pw spirv_probe shim ----
#ifndef PW_G_SMP
#define PW_G_SMP
SamplerState g_smp;
#endif
"""

# Макет дескриптор-сетов §3.z п.1. DXC трактует HLSL `space` как descriptor set
# (замер 2026-10-15: register(t0, space2)/... даёт sets [0,1,2,3]), поэтому
# раскладка делается здесь, а не флагами -fvk-*-shift (на явно зарегистрированные
# ресурсы они не действуют). set 0 остаётся авто-$Globals.
SPACE_TEXTURES = 2
SPACE_SAMPLERS = 3
SPACE_BUFFER   = 1     # SSBO динамических массивов (материал)

HEADER_SET = """
// ---- pw spirv_probe shim (set layout) ----
#ifndef PW_G_SMP
#define PW_G_SMP
SamplerState g_smp : register(s0, space%d);
#endif
""" % SPACE_SAMPLERS

SAMPLER_DECL = re.compile(
    r"\b(sampler1D|sampler2D|sampler3D|samplerCUBE|sampler|sampler_state|samplerSOM)\s+(\w+)\s*(\[[^\]]*\])?"
    r"\s*(?::\s*register\s*\(([^)]*)\))?\s*;")
REG_ANY = re.compile(r"\s*:\s*register\s*\(([^)]*)\)")

# Макросы слотов ресурсов из GlobalMasks.h: `#define WARFOGSAMPLERINDEX s10`.
# DXC макросов не знает, а `register( WARFOGSAMPLERINDEX )` без раскрытия тихо
# превращается в binding 0: замер 2026-10-18 по vk_rec7.pwrd — у
# FullScreenMaterial #113 %warFogSampler и %DepthMap получили один и тот же
# (DescriptorSet 2, Binding 0), warFog читал R32F-буфер глубины вместо 205x205
# карты, и полноэкранный композит (72% кадра, проба цвета по парам) давал чёрный.
# Классы: t=текстура, s=сэмплер, b/u=буфер (cbank.py разбирает те же строки для
# c-регистров; здесь — только ресурсы).
SLOT_DEFINE = re.compile(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+([tsub])\s*(\d+)"
                         r"\s*(//.*)?$")


def slot_macros():
    """GlobalMasks.h -> {ИМЯ: 's10'}; пустой словарь, если файла нет."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", "pw", "branches", "r1117",
                        "Src", "Render", "GlobalMasks.h")
    out = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = SLOT_DEFINE.match(line)
                if m:
                    out[m.group(1)] = m.group(2) + m.group(3)
    except OSError:
        pass
    return out


SLOT_MACROS = slot_macros()


def expand_reg(reg):
    """`WARFOGSAMPLERINDEX` -> `s10`; числовое или неизвестное — без изменений."""
    return SLOT_MACROS.get((reg or "").strip(), (reg or "").strip())

# Объявление константы c-банка: [const|uniform|static|extern] ТИП имя[массив] : register( МАКРОС|cN );
# Префикс `uniform` обязан быть в альтернативе (замер 2026-10-18, прогон 98 —
# отсутствующий HUD): 20 объявлений вида `uniform float4 color1 : register(
# VSHADER_LOCALCONST0 );` в _commonUI.hlsl / UIFlashMaterial.hlsl без префикса
# не матчились, член не попадал в канонический банк, DXC паковал их в свой
# `$Globals` (Offset 0,16,...), а читался банк клиента (Offset = reg*16):
# color1 (c20) читался из c0 — HUD выходил мусорным/прозрачным.
# Один источник для прослойки (гасит их в режиме канонического банка) и для
# tools/shader_build/cbank.py (строит из них банк).
CDECL = re.compile(
    r"^[ \t]*(?P<const>const\s+|uniform\s+|static\s+|extern\s+)?(?P<type>float4x4|float4x3|float4x2|float3x4|"
    r"float3x3|float2x4|float2x2|matrix|float4|float3|float2|float|half4|half3|"
    r"half2|half|int4|int3|int2|int|bool4|bool3|bool2|bool|uint4|uint3|uint2|uint)"
    r"[ \t]+(?P<name>\w+)[ \t]*(?P<arr>\[[^]]*\])?[ \t]*:[ \t]*register[ \t]*"
    r"\([ \t]*(?P<reg>c[ \t]*\d+|[A-Z_][A-Z0-9_]*)[ \t]*\)[ \t]*;",
    re.M)


def strip_cdecls(src):
    """Снимает объявления c-банка (имена приходят из сгенерированного банка)."""
    out, n = CDECL.subn("", src)
    return out, n
# ресурсы, у которых в исходниках register нет (наши StructuredBuffer из шага 10
# и Texture2D, выросшие из sampler2D) — им номер назначаем здесь
AUTO_RES = re.compile(r"\b(Texture2D|Texture2DArray|TextureCube|StructuredBuffer|"
                      r"RWStructuredBuffer|ByteAddressBuffer|SamplerState)"
                      r"(\s*<[^<>]*>)?\s+(\w+)\s*(\[[^\]]*\])?\s*;")

_TEXCOORD_CTR = [200]

def rewrite(src, stage=None, ps_ret_structs=(), vs_ret_structs=(), set_layout=False,
            cbank=False):
    notes = []
    _TEXCOORD_CTR[0] = 100   # счётчик — на файл, иначе семантика уезжает за лимит
    # 0. макросы слотов ресурсов — ДО проходов register(...) и strip_cdecls,
    #    иначе от аннотации остаётся пустота и DXC нумерует binding сам.
    def macro_reg(m):
        e = expand_reg(m.group(1))
        if e == (m.group(1) or "").strip():
            return m.group(0)
        notes.append("макрос слота: register(%s) -> register(%s)"
                     % (m.group(1).strip(), e))
        return ": register(%s)" % e
    src, n_mac = REG_ANY.subn(macro_reg, src)
    if n_mac:
        notes.append("раскрыто макросов слотов: %d" % n_mac)
    # 0. канонический банк: объявления c-констант снимаются совсем — имена
    #    приходят из сгенерированного `cbuffer pwCBankXX` (см. cbank.bank_block).
    #    Делается ДО прохода register(...), иначе от объявления останется голый
    #    `float4 имя;` и член банка продублируется на авто-$Globals.
    if cbank:
        src, n_cb = strip_cdecls(src)
        if n_cb:
            notes.append("cbank: снято объявлений c-банка: %d" % n_cb)
    # 1. combined-сэмплеры -> ресурс; сам SamplerState один общий
    def sampler_sub(m):
        kind, name = m.group(1), m.group(2)
        arr = m.group(3) or ""   # sampler NAME[4] : register(...) - массив ресурсов
        reg = m.group(4) or ""
        # Номер D3D9-сэмплера обязан стать номером binding в наборе текстур:
        # клиент привязывает текстуру вызовом SetTexture(samplerIndex, ...) по
        # номеру слота D3D9 (GlobalMasks.h: WARFOGSAMPLERINDEX s10,
        # GLOBALENVIROMENTSAMPLERINDEX s12, TERRAIN_DIFFUSESAMPLER0..3 = s0..s3),
        # a прослойка до этой правки нумеровала Texture2D по порядку
        # объявлений — соответствие «слот клиента -> binding» терялось
        # молча, текстура уезжала в чужой слот без ошибки.
        # sampler_state (чистый сэмплер) остаётся в наборе сэмплеров под своим
        # номером; в живом корпусе sampler_state не встречается (замер по
        # Src/Render/Shaders/*.hlsl — 0 объявлений).
        keep = ""
        if set_layout:
            mm = re.match(r"\s*([tsub])\s*(\d+)\s*$", reg)
            if mm:
                cls = "s" if kind == "sampler_state" else "t"
                keep = " : register(%s%s, space%d)" % (
                    cls, mm.group(2), SPACE_SAMPLERS if cls == "s" else SPACE_TEXTURES)
        if kind == "samplerCUBE":
            notes.append("samplerCUBE %s" % name)
            return "TextureCube %s%s%s;" % (name, arr, keep)
        if kind == "sampler_state":
            notes.append("sampler_state %s" % name)
            return "SamplerState %s%s;" % (name, arr, keep)
        notes.append("%s %s%s" % (kind, name, keep))
        return "Texture2D %s%s%s;" % (name, arr, keep)
    src, n_samp = SAMPLER_DECL.subn(sampler_sub, src)

    # 2. register(...) во всех объявлениях — снимаем (c-регистры), а для
    #    t/s/b/u в режиме макета оставляем номер и добавляем space
    if set_layout:
        SPACE_OF = {"t": SPACE_TEXTURES, "s": SPACE_SAMPLERS,
                    "b": SPACE_BUFFER, "u": SPACE_BUFFER}
        def reg_keep(m):
            mm = re.match(r"\s*([tsub])\s*(\d+)", m.group(1))
            if not mm:
                return ""            # c-регистры -> авто-$Globals
            cls = mm.group(1)
            return " : register(%s%s, space%d)" % (cls, mm.group(2), SPACE_OF[cls])
        src, n_reg = REG_ANY.subn(reg_keep, src)

        ctr = {"t": 0, "s": 1, "b": 0}   # s0 уже занят общим g_smp
        def auto_res(m):
            base = m.group(1)
            typ, name, arr = base + (m.group(2) or ""), m.group(3), (m.group(4) or "")
            if base == "SamplerState":
                cls = "s"
            elif base in ("StructuredBuffer", "RWStructuredBuffer", "ByteAddressBuffer"):
                cls = "b"
            else:
                cls = "t"
            n = ctr[cls]; ctr[cls] += 1
            # DXC для StructuredBuffer требует класс 't' (замер: register(u0) ->
            # «invalid register specification, expected 't' binding»)
            regcls = "t" if cls == "b" else cls
            notes.append("set-layout: %s %s -> %s%d space%d"
                         % (typ, name, regcls, n, SPACE_OF[cls]))
            return "%s %s%s : register(%s%d, space%d);" % (typ, name, arr, regcls, n,
                                                           SPACE_OF[cls])
        src, n_auto = AUTO_RES.subn(auto_res, src)
    else:
        src, n_reg = REG_ANY.subn("", src)
        n_auto = 0

    # 3. выборки
    # operand may be an array element: tex2D( diffuseMapSampler[1], uv )
    src, n_t2 = re.subn(r"\btex2D\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,", r"\1.Sample(g_smp,", src)
    # tex1D( t, u ) -> t.Sample(g_smp, float2(u, 0.5)) — seam создаёт 1D-текстуры
    # как 2D (высота 1), отдельного измерения в наборе дескрипторов нет
    # (замер: UpscaleH/UpscaleV `sampler1D weights : register(s1)` + tex1D).
    src, n_t1 = re.subn(r"\btex1D\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,\s*([^;()]*(?:\([^()]*\))?[^;()]*)\)",
                        r"\1.Sample(g_smp, float2(\2, 0.5))", src)
    src, n_cube = re.subn(r"\btexCUBE\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,", r"\1.Sample(g_smp,", src)
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
        return "%s.Sample(g_smp, (%s).xy / (%s).w)" % (t, c, c)
    # 2-й аргумент — выражение, а не только имя (замер: UberShadowPassDST.hlsl
    # `tex2Dproj(shadowMap, PositionLS + texelShift.xxzz)` — из-за ограничителя
    # `\w+` ShadowPassPCFDST4/4Add не собирались вообще)
    src, n_proj = re.subn(
        r"\btex2Dproj\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,\s*([^;()]*(?:\([^()]*\))?[^;()]*)\)",
        proj_sub, src)

    # 4. семантики вывода.
    #    POSITION на выходе VS -> SV_POSITION;
    #    COLOR на выходе PS    -> SV_TARGET, а в структурах (вход/выход) —
    #    в нейтральную TEXCOORDn: в SM6 COLOR в профиле ps недопустим.
    # POSITION -> SV_POSITION только у структуры, которой возвращает себя VS:
    # у входа VS POSITION — это атрибут, и системной семантикой он быть не
    # должен (в 67/70 выше грубое правило не ломало сборку, но оставлять его
    # нельзя: DXC трактует SV_POSITION на входе как пользовательскую семантику).
    n_pos = 0
    for st in vs_ret_structs:
        sm = re.search(r"struct\s+%s\s*\{(.*?)\n\s*\};" % st, src, re.S)
        if sm:
            newbody, k = re.subn(r":\s*POSITION\b", ": SV_POSITION", sm.group(1))
            src = src[:sm.start(1)] + newbody + src[sm.end(1):]
            n_pos += k
    mret = re.search(r"(\w+_VSMain\s*\([^)]*\))\s*:\s*POSITION\b", src)
    if mret:
        src = src[:mret.end() - 8] + "SV_POSITION" + src[mret.end():]
        n_pos += 1
    # семантика возврата самой точки входа (у материала их может быть несколько —
    # по одному на проход)
    if stage == "ps":
        src = re.sub(r"(\w+_PSMain\s*\([^)]*\))\s*:\s*COLOR\d*\b",
                     lambda m: m.group(1) + " : SV_TARGET", src)
        # out-параметры PS — это тоже выходы в мишень; в SM6 произвольная
        # пользовательская семантика на выходе запрещена («invalid usage of
        # semantic 'TEXCOORD101' in shader profile ps» на WaterDummyMaterial),
        # поэтому индекс сохраняем: COLOR1 -> SV_TARGET1 (возврат функции при
        # этом выше стал SV_TARGET, то есть SV_TARGET0).
        src = re.sub(r"(out\s+\w+\s+\w+\s*:\s*)COLOR(\d*)\b",
                     lambda m: m.group(1) + "SV_TARGET" + (m.group(2) or "0"), src)
    elif stage == "vs":
        src = re.sub(r"(\w+_VSMain\s*\([^)]*\))\s*:\s*COLOR\d*\b",
                     lambda m: m.group(1) + " : TEXCOORD190", src)
        # 28 материалов из 49 отдают клиповую позицию out-параметром
        # `out float4 PositionCS : POSITION`. В SM6 системная семантика —
        # SV_POSITION; без подмены DXC считает POSITION пользовательской и
        # gl_Position в SPIR-V не пишется вообще (замер: у OpEntryPoint в
        # interface только пользовательские переменные, BuiltIn-декораций
        # нет) — вершина уходит в неопределённую позицию.
        src, n_outpos = re.subn(r"(out\s+\w+\s+\w+\s*:\s*)POSITION\b",
                                lambda m: m.group(1) + "SV_POSITION", src)
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
        # Детерминированное имя: COLORn -> TEXCOORD(100+n). Прежний счётчик по
        # порядку в файлу (первый COLOR = TEXCOORD101, второй = TEXCOORD102,
        # независимо от индекса) делал сопоставление атрибута клиента с location
        # гадалкой: на стенде COLOR0 попадал на вход, который DXC назначил
        # COLOR3, и вершинные данные разъезжались молча.
        idx = m.group(1)
        return ": TEXCOORD%d" % (100 + (int(idx) if idx else 0))
    src, n_col = re.subn(r":\s*COLOR(\d*)\b", color_field, src)

    # PS-вход: позиция в структуре-параметре — это выход VS (мы только что
    # сделали из него SV_POSITION). Если PS-точка входа одновременно объявляет
    # VPOS (-> SV_Position), DXC видит два входа с FragCoord и отказывает:
    # «OpEntryPoint contains duplicate input variables with FragCoord builtin»
    # (UIFlashMaterial). Для PS эта позиция — обычный интерполятор, поэтому
    # уводим её в незанятую пользовательскую семантику; если тело PS её и
    # правда читает, это работа для шага 13 (исходник обязан перейти на
    # PixelPos), и мы помечаем материал в notes.
    n_pspos = 0
    if stage == "ps":
        for pm in re.finditer(r"^\s*\w[\w:]*\s+\w+_PSMain\s*\(\s*(\w+)", src, re.M):
            st = pm.group(1)
            sm = re.search(r"struct\s+%s\s*\{(.*?)\n\s*\};" % st, src, re.S)
            if not sm:
                if re.search(r"\b\w+_PSMain\s*\(\s*%s" % st, src) and \
                        re.search(r"\bVPOS\b", src):
                    notes.append("PS-структура %s вне файла — позицию из PS-входа "
                                 "не убрали" % st)
                continue
            newbody, k = re.subn(r":\s*(SV_POSITION|POSITION)\b", ": TEXCOORD191",
                                 sm.group(1))
            if k:
                src = src[:sm.start(1)] + newbody + src[sm.end(1):]
                n_pspos += k
                if re.search(r"\bVPOS\b", src):
                    notes.append("PS %s: позиция убрана из входа, нужен PixelPos"
                                 % st)

    src, n_vpos = re.subn(r"\bVPOS\b", "SV_Position", src)
    # FragCoord по Vulkan-спеке — обязательно 4-компонентный; в исходниках
    # float2 PixelPos : VPOS, поэтому расширяем до float4 (использование .xy не меняется)
    src, n_frag = re.subn(r"\b(float2)\s+(\w+\s*:\s*SV_Position)", r"float4 \2", src)
    src, n_vface = re.subn(r"\bVFACE\b", "SV_IsFrontFace", src)

    stats = dict(samplers=n_samp, regs=n_reg, tex2D=n_t2, tex1D=n_t1, texCUBE=n_cube,
                 tex2Dlod=n_lod + n_lod_var + n_lod3, tex2Dproj=n_proj, position=n_pos,
                 color=n_col, ps_position=n_pspos, vpos=n_vpos + n_frag, vface=n_vface,
                 regs_auto=n_auto, out_position=n_outpos if stage == "vs" else 0)
    return (HEADER_SET if set_layout else HEADER) + src, stats, notes

if __name__ == "__main__":
    import sys
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    out, st, notes = rewrite(src)
    sys.stdout.write(out)
    sys.stderr.write(str(st) + "\n")
