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
#                                                 + SamplerState g_smp_N по номеру
#                                                  D3D9-слота (в режиме макета);
#                                                  без макета — один общий g_smp
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

# D3D9 привязывает sampler-state к СЛОТУ: SetSamplerState(N, st) действует на
# выборку tex2D(sN, ...). Прослойка до правки объявляла ОДИН g_smp (register s0)
# и подставляла его во все выборки, а seam считает binding == D3D9-слот
# (nvrhibackend.cpp: «SetTexture(N, tex) кладёт текстуру прямо в binding N»),
# значит все выборки прохода получали состояние слота 0. Замер 2026-10-23
# (пост-проход, PS #122): fog-текстура (рег. 10: CLAMPTOEDGE, mipF=NONE) читалась
# состоянием слота 0 (WRAP, mipF=LINEAR) -> fog-карта 205x205 тайлилась за
# пределами [0,1] -> по экрану рядные полосы периода 45..55 px. Аудит исходников:
# 31 файл из 88 выбирают из >=2 разных слотов (TerrainElement 0..4, Water 0..5,
# Road 0..6, UIButton 0..6, FullScreen 0,1,2,3,10, NatureMap 0..4, WarfogBlend
# 0,1, ShadowPass*, UpscaleH/V, ...). Правка: на каждый используемый номер
# слота — свой SamplerState g_smp_N : register(sN, space3).
SMP_VAR = "g_smp"          # общий сэмплер: выборки, у которых слот не известен


def smp_name(reg):
    return "%s_%d" % (SMP_VAR, reg)


INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def include_texts(path, depth=3):
    """Тексты локальных `#include "..."` (только для разбора объявлений ресурсов).

    Объявления сэмплеров живут и в включаемых `_common*.hlsl`; прослойка работает
    по одному файлу, поэтому без этого обхода выборка из материала, у которого
    сэмплер объявлен в include, не знает свой слот и остаётся на общем g_smp
    (замер: envMapSampler из _common.hlsl = register(GLOBALENVIROMENTSAMPLERINDEX)
    читался состоянием слота 0). Слот из include важен и для номера binding: его
    назначает прослойка в том файле, где объявление.
    """
    seen, out = set(), []

    def walk(p, d):
        p = os.path.normpath(p)
        if d < 0 or p in seen or not os.path.isfile(p):
            return
        seen.add(p)
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                txt = f.read()
        except OSError:
            return
        if p != os.path.normpath(path):
            out.append(txt)
        base = os.path.dirname(p)
        for m in INCLUDE_RE.finditer(txt):
            rel = m.group(1)
            for cand in (os.path.join(base, rel), os.path.join(os.path.dirname(base), rel)):
                if os.path.isfile(cand):
                    walk(cand, d - 1)
    walk(path, depth)
    return out


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
            cbank=False, src_path=None):
    notes = []
    # имя текстуры -> номер D3D9-слота; слот -> имя уже объявленного SamplerState
    tex_reg = {}
    state_of_reg = {}
    state_reg_taken = set()
    need_regs = set()   # слоты, реально используемые выборками этого файла
    decl_extra = []     # алиасы ресурсов (пер-слотовые элементы массивов)
    if src_path:
        for extra in include_texts(src_path):
            for m in SAMPLER_DECL.finditer(extra):
                if m.group(2) in tex_reg or m.group(2) in state_of_reg:
                    continue
                reg = expand_reg(m.group(4) or "")
                mm = re.match(r"\s*[ts]\s*(\d+)\s*$", reg)
                if not mm:
                    continue
                if m.group(1) == "sampler_state":
                    state_of_reg[int(mm.group(1))] = m.group(2)
                    state_reg_taken.add(int(mm.group(1)))
                else:
                    tex_reg[m.group(2)] = int(mm.group(1))
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
        # Макросы слотов (GlobalMasks.h) обязаны быть раскрыты ДО разбора: без
        # этого `register( WARFOGSAMPLERINDEX )` не матчится ниже, keep пустой,
        # и DXC сам назначает binding 0 — в один binding с DepthMap (замер
        # 2026-10-20: регенерированный корпус FullScreenMaterial дал красный
        # экран, warFog читал карту глубины как цвет).
        reg = expand_reg(reg)
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
        if kind == "sampler_state":
            notes.append("sampler_state %s" % name)
            mm = re.match(r"\s*[ts]\s*(\d+)\s*$", reg)
            if mm:      # чистый сэмплер уже занимает слот — выборки пойдут в него
                state_of_reg[int(mm.group(1))] = name
                state_reg_taken.add(int(mm.group(1)))
            return "SamplerState %s%s;" % (name, arr, keep)
        mm = re.match(r"\s*[ts]\s*(\d+)\s*$", reg)
        if mm:
            tex_reg[name] = int(mm.group(1))
            # Массив сэмплеров (`sampler a[4] : register(s0)`) в D3D9 — НЕСКОЛЬКО
            # слотов: a[i] = слот base+i, и клиент кладёт в каждый свою текстуру
            # (TERRAIN_DIFFUSESAMPLER0..3 = s0..s3). Прослойка объявляла один
            # Texture2D на базовом binding — выборки a[1..3] читали текстуру
            # слота 0. Для литеральных индексов объявляем алиасы a_<i> на своих
            # binding'ах; динамический индекс DXC и так запрещает.
            am = re.match(r"\s*\[\s*(\d+)\s*\]\s*$", arr)
            if set_layout and am:
                base, cnt = int(mm.group(1)), int(am.group(1))
                extra = []
                for i in range(1, min(cnt, 16)):
                    alias = "%s_%d" % (name, i)
                    tex_reg[alias] = base + i
                    extra.append("Texture%s %s%s : register(t%d, space%d);" % (
                        "Cube" if kind == "samplerCUBE" else "2D",
                        alias, "", base + i, SPACE_TEXTURES))
                if extra:
                    notes.append("per-slot массив %s: алиасы %s" % (
                        name, ",".join("%s_%d" % (name, i) for i in range(1, min(cnt, 16)))))
                    decl_extra.extend(extra)
        if kind == "samplerCUBE":
            notes.append("samplerCUBE %s" % name)
            return "TextureCube %s%s%s;" % (name, arr, keep)
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
            n = ctr[cls]
            if cls == "s":
                # номера слотов, занятые per-slot сэмплерами (g_smp_N) и
                # sampler_state из исходника, авто-назначать нельзя
                while n in state_reg_taken or n in tex_reg.values():
                    n += 1
            ctr[cls] = n + 1
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
    def smp_for(op):
        """`a[1]` -> (сэмплер слота base+1, переменная текстуры этого слота)."""
        m = re.match(r"\s*(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*$", op)
        if not m:
            return SMP_VAR, op.strip()
        base, idx = m.group(1), m.group(2)
        var = op.strip()          # как в исходнике: `a[0]` остаётся `a[0]`
        reg = tex_reg.get(base)
        if idx:
            alias = "%s_%s" % (base, int(idx))
            if alias in tex_reg:
                var, reg = alias, tex_reg[alias]
            elif reg is not None:
                reg += int(idx)   # слот base+i; текстура — элемент массива
        if reg is None:
            return SMP_VAR, var
        if reg in state_of_reg:
            return state_of_reg[reg], var
        if reg == 0:
            return SMP_VAR, var
        need_regs.add(reg)
        return smp_name(reg), var

    def sub_tex(pat, fmt, flags=0):
        """подмена выборки: {0}=текстура, {1}=её сэмплер, {2..}=остальные
        группы паттерна (кроме группы 1 — это текстура). Замер: `*groups()[2:]`
        терял вторую группу, и на `tex2Dlod(t, VAR)` / `tex1D(t, u)` шаблон
        падал IndexError (срез онлайн-шейдеров, 44 сайдкара)."""
        def f(m):
            smp, tex = smp_for(m.group(1))
            return fmt.format(tex, smp, *m.groups()[1:])
        return re.subn(pat, f, src, flags=flags)

    src, n_t2 = sub_tex(r"\btex2D\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,", "{0}.Sample({1},")
    # tex1D( t, u ) -> t.Sample(g_smp, float2(u, 0.5)) — seam создаёт 1D-текстуры
    # как 2D (высота 1), отдельного измерения в наборе дескрипторов нет
    # (замер: UpscaleH/UpscaleV `sampler1D weights : register(s1)` + tex1D).
    src, n_t1 = sub_tex(r"\btex1D\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,\s*([^;()]*(?:\([^()]*\))?[^;()]*)\)",
                        "{0}.Sample({1}, float2({2}, 0.5))")
    src, n_cube = sub_tex(r"\btexCUBE\s*\(\s*(\w+\s*(?:\[[^\]]*\])?)\s*,", "{0}.Sample({1},")
    # tex2Dlod( t, float4( uv2, 0, 0 ) ) -> t.SampleLevel(g_smp, uv2, 0)
    src, n_lod3 = re.subn(
        r"\btex2Dlod\s*\(\s*(\w+)\s*,\s*float4\s*\(\s*([^,()]+)\s*,\s*[^,()]+\s*,\s*[^,()]+\s*\)\s*\)",
        r"\1.SampleLevel(g_smp, \2, 0)", src)
    # tex2Dlod( t, VAR ) -> t.SampleLevel(g_smp, VAR.xy, VAR.w)  (общая форма)
    src, n_lod_var = sub_tex(r"\btex2Dlod\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                             "{0}.SampleLevel({1}, {2}.xy, {2}.w)")
    # tex2Dlod( t, float4( u,v,0,mip ) ) -> t.SampleLevel(g_smp, float2(u,v), mip)
    def lod_sub(m):
        t, a, b, d = m.group(1), m.group(2), m.group(3), m.group(4)
        return "%s.SampleLevel(%s, float2(%s,%s), %s)" % (t, smp_for(t)[0], a, b, d)
    src, n_lod = re.subn(
        r"\btex2Dlod\s*\(\s*(\w+)\s*,\s*float4\s*\(\s*([^,()]+)\s*,\s*([^,()]+)\s*,\s*[^,()]+\s*,\s*([^,()]+)\s*\)\s*\)",
        lod_sub, src)
    # tex2Dproj( t, float4 ) -> t.Sample(g_smp, c.xy / c.w)
    def proj_sub(m):
        t, c = m.group(1), m.group(2)
        return "%s.Sample(%s, (%s).xy / (%s).w)" % (t, smp_for(t)[0], c, c)
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
    # per-slot сэмплеры объявляем в шапке ПОСЛЕ обхода исходника: номера известны
    # только после разбора объявлений и выборок. Объявления в шапке — до первого
    # использования, порядок корректен.
    head = HEADER_SET if set_layout else HEADER
    if set_layout and (need_regs or decl_extra):
        # каждое объявление — СВОИМ include-guard'ом и СНАРУЖИ блока PW_G_SMP:
        # заголовок prepend'ится к каждому файлу компиляционного юнита, а общий
        # guard PW_G_SMP (одна блокировка на всех) глушит объявления второго
        # файла: замер — _commonDistortion.hlsl не получал g_smp_13/g_smp_14,
        # которые нужны ему самому (материал их не выбирает, его заголовок их
        # не объявляет). Именной guard = дедупликация по всему юниту.
        parts = []
        for d in decl_extra:
            nm = d.split()[1].split(":")[0]
            parts.append("#ifndef PW_RES_%s\n#define PW_RES_%s\n%s\n#endif\n" % (nm, nm, d))
        need = sorted(need_regs - {0} - set(state_of_reg))
        for r in need:
            parts.append("#ifndef PW_SMP_%d\n#define PW_SMP_%d\n"
                         "SamplerState %s : register(s%d, space3);\n#endif\n"
                         % (r, r, smp_name(r), r))
        head = HEADER_SET + "".join(parts)
        if need:
            notes.append("per-slot сэмплеры: %s" % ",".join(str(r) for r in need))
    return head + src, stats, notes

if __name__ == "__main__":
    import sys
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    out, st, notes = rewrite(src)
    sys.stdout.write(out)
    sys.stderr.write(str(st) + "\n")
