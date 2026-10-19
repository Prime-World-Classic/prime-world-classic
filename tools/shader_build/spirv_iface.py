#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""spirv_iface.py — разбор и правка интерфейса SPIR-V (varying'и, location).

Зачем. D3D9 сопоставляет varying'и пары VS/PS по ИМЕНИ семантики, Vulkan — по
LOCATION. DXC назначает location каждому шейдеру независимо — по порядку
полей во входной/выходной структуре (неиспользованное поле вырезается мёртвым
кодом, но номер занимает). Структуры VS-выхода и PS-входа в исходниках
объявлены в разном порядке и с разными наборами полей, поэтому у реальной пары
один и тот же TEXCOORD3 оказывается на разных location, а у VS остаётся
«дырка», которой нет в вершинной декларации клиента. Отсюда VUID из валидатора
(§6, шаг 12):

  VUID-VkGraphicsPipelineCreateInfo-Input-07904  вход VS есть, в layout нет
  VUID-VkGraphicsPipelineCreateInfo-Input-08733  класс формата != тип входа VS
  VUID-RuntimeSpirv-OpEntryPoint-08743           вход PS есть, выхода VS нет

Лечение — каноническая карта «семантика -> location», применяемая ко всем
шейдерам контейнера (localign.py). Здесь — механизмы: disasm/assemble, разбор
интерфейса, перестановка location.

Раунд-трип dis->as проверен на реальном блобе (MainPass_VSMain, 1508 Б):
размер совпадает побайтово, spirv-val проходит. Правка location меняет только
операнд OpDecorate, то есть не трогает ни одну инструкцию кроме декораций.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

SPIRV_MAGIC = 0x07230203
OP_MATRIX_TIMES_VECTOR = 145
OP_VECTOR_TIMES_MATRIX = 144

# порядок семейств семантик для канонической карты (D3D9-порядок константных
# регистров здесь ни при чём — это просто детерминированный порядок слотов)
FAMILY_RANK = {
    "POSITION": 0, "COLOR": 1, "NORMAL": 2, "TANGENT": 3, "BINORMAL": 4,
    "TEXCOORD": 5, "PSIZE": 6, "FOG": 7, "BLENDINDICES": 8, "BLENDWEIGHT": 9,
    "VFACE": 10, "VPOS": 11,
}


def split_sem(name):
    """'in.var.TEXCOORD3' -> ('TEXCOORD', 3, 'TEXCOORD3').

    Имена интерфейсных переменных DXC: `in.var.<SEM>` / `out.var.<SEM>`.
    Семантики D3D9 регистронезависимы и всегда «семейство+число» (голый
    POSITION = POSITION0).
    """
    n = name.split(".")[-1].upper()
    n = {"POSITION": "POSITION0", "COLOR": "COLOR0", "NORMAL": "NORMAL0",
         "TEXCOORD": "TEXCOORD0", "TANGENT": "TANGENT0",
         "BINORMAL": "BINORMAL0", "FOG": "FOG0", "PSIZE": "PSIZE0"}.get(n, n)
    m = re.match(r"^([A-Z]+)(\d+)$", n)
    if not m:
        return (n, 0, n)
    return (m.group(1), int(m.group(2)), n)


def canon_key(sem):
    fam, idx, name = split_sem(sem)
    return (FAMILY_RANK.get(fam, 90), idx, name)


def build_loc_map(semantics, start=0):
    """Каноническая карта {семантика: location} по плотному порядку.

    semantics — множество имён семантик (уже после прослойки shim.py: COLORn
    там живут как TEXCOORD(100+n)). Возвращает dict; порядок детерминирован,
    поэтому повторная сборка даёт побайтово тот же SPIR-V.
    """
    out = {}
    for i, sem in enumerate(sorted(semantics, key=canon_key)):
        out[sem] = start + i
    return out


ID = r"[%A-Za-z_][%\w.]*"
RE_NAME = re.compile(r"^\s*OpName\s+(%s)\s+\"(.*?)\"" % ID)
RE_ENTRY = re.compile(r"^\s*OpEntryPoint\s+(\w+)\s+(%s)\s+\"(.*?)\"" % ID)
RE_LOC = re.compile(r"^(\s*OpDecorate\s+)(%s)(\s+Location\s+)(\d+)(.*)$" % ID)
RE_BUILTIN = re.compile(r"^\s*OpDecorate\s+(%s)\s+BuiltIn\s+(\w+)" % ID)
RE_VAR_IN = re.compile(r"^\s*(%s)\s*=\s*OpVariable\s+%s\s+Input\b" % (ID, ID))
RE_VAR_OUT = re.compile(r"^\s*(%s)\s*=\s*OpVariable\s+%s\s+Output\b" % (ID, ID))
RE_PTR = re.compile(r"^\s*(%s)\s*=\s*OpTypePointer\s+(\w+)\s+(%s)" % (ID, ID))
RE_VEC = re.compile(r"^\s*(%s)\s*=\s*OpTypeVector\s+(%s)\s+(\d+)" % (ID, ID))
RE_MAT = re.compile(r"^\s*(%s)\s*=\s*OpTypeMatrix\s+(%s)\s+(\d+)" % (ID, ID))
RE_FLOAT = re.compile(r"^\s*(%s)\s*=\s*OpTypeFloat\s+(\d+)" % ID)
RE_INT = re.compile(r"^\s*(%s)\s*=\s*OpTypeInt\s+(\d+)\s+(\d+)" % ID)
RE_IMG = re.compile(r"^\s*(%s)\s*=\s*OpTypeImage\b" % ID)
RE_SIMG = re.compile(r"^\s*(%s)\s*=\s*OpTypeSampledImage\s+(%s)" % (ID, ID))
RE_STR = re.compile(r"^\s*(%s)\s*=\s*OpTypeStruct\b" % ID)
RE_ARR = re.compile(r"^\s*(%s)\s*=\s*OpTypeArray\s+(\S+)\s+(\S+)" % ID)
RE_RTARR = re.compile(r"^\s*(%s)\s*=\s*OpTypeRuntimeArray\s+(\S+)" % ID)
RE_RES = re.compile(r"^\s*(%s)\s*=\s*OpVariable\s+(%s)\s+"
                    r"(UniformConstant|Uniform|StorageBuffer|StorageImage)\b" % (ID, ID))


def disasm(spv, extra=()):
    """SPIR-V -> текст листинга (SPIRV-Tools). None + текст ошибки при провале."""
    with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as f:
        f.write(spv)
        p = f.name
    try:
        r = subprocess.run(["spirv-dis", "--no-header", *extra, p],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, (r.stderr or r.stdout).strip()
        return r.stdout, None
    finally:
        os.unlink(p)


def assemble(txt, target="vulkan1.0"):
    """листинг -> SPIR-V. Возвращает (байты, ошибка)."""
    with tempfile.NamedTemporaryFile(suffix=".dis", mode="w", delete=False,
                                     encoding="utf-8") as f:
        f.write(txt)
        p = f.name
    out = p + ".spv"
    try:
        r = subprocess.run(["spirv-as", "--target-env", target, p, "-o", out],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, (r.stderr or r.stdout).strip()
        return open(out, "rb").read(), None
    finally:
        os.unlink(p)
        if os.path.exists(out):
            os.unlink(out)


def validate(spv, target="vulkan1.0"):
    with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as f:
        f.write(spv)
        p = f.name
    try:
        r = subprocess.run(["spirv-val", "--target-env", target, p],
                           capture_output=True, text=True)
        return r.returncode == 0, (r.stderr or r.stdout).strip()
    finally:
        os.unlink(p)


def parse_iface(txt):
    """Интерфейс по листингу: stage, 'in'/'out' — списки переменных, 'res' —
    дескрипторы. Типы свёрнуты к (класс, ширина)."""
    names, ptrs, types = {}, {}, {}
    builtin, loc = {}, {}
    stage, entry = None, None
    vars_in, vars_out, res = [], [], []
    for line in txt.split("\n"):
        m = RE_NAME.match(line)
        if m:
            names[m.group(1)] = m.group(2)
            continue
        m = RE_ENTRY.match(line)
        if m:
            stage, entry = m.group(1), m.group(2)
            continue
        m = RE_BUILTIN.match(line)
        if m:
            builtin[m.group(1)] = m.group(2)
            continue
        m = RE_LOC.match(line)
        if m:
            loc[m.group(2)] = (int(m.group(4)),
                               int(m.group(5).split()[0]) if m.group(5).strip()
                               else 0)
            continue
        m = RE_PTR.match(line)
        if m:
            ptrs[m.group(1)] = (m.group(2), m.group(3))
            continue
        for rx, cls in ((RE_VEC, "vec"), (RE_MAT, "mat")):
            m = rx.match(line)
            if m:
                types[m.group(1)] = (cls, m.group(2), int(m.group(3)))
                break
        else:
            m = RE_FLOAT.match(line)
            if m:
                types[m.group(1)] = ("float", None, 1)
                continue
            m = RE_INT.match(line)
            if m:
                types[m.group(1)] = ("sint" if m.group(3) == "1" else "uint",
                                     None, 1)
                continue
            m = RE_IMG.match(line)
            if m:
                types[m.group(1)] = ("image", None, None)
                continue
            m = RE_SIMG.match(line)
            if m:
                types[m.group(1)] = ("sampled", m.group(2), None)
                continue
            m = RE_ARR.match(line)
            if m:
                types[m.group(1)] = ("array", m.group(2), None)
                continue
            m = RE_RTARR.match(line)
            if m:
                types[m.group(1)] = ("runtime", m.group(2), None)
                continue
            m = RE_STR.match(line)
            if m:
                types[m.group(1)] = ("struct", None, None)
                continue
        m = RE_VAR_IN.match(line)
        if m:
            vars_in.append(m.group(1))
            continue
        m = RE_VAR_OUT.match(line)
        if m:
            vars_out.append(m.group(1))
            continue
        m = RE_RES.match(line)
        if m:
            res.append({"id": m.group(1), "ptr": m.group(2),
                        "name": names.get(m.group(1), m.group(1))})
            continue

    def info(var):
        l = loc.get(var)
        raw = names.get(var, var)
        sem = split_sem(raw)[2]
        return {"id": var, "name": raw, "sem": sem,
                "loc": None if l is None else l[0],
                "comp": 0 if l is None else l[1],
                "builtin": builtin.get(var)}

    return {"stage": stage, "entry": entry,
            "in": [info(v) for v in vars_in],
            "out": [info(v) for v in vars_out],
            "res": res, "names": names, "ptrs": ptrs, "types": types,
            "loc": loc, "builtin": builtin}


def interface_vars(spv):
    """(stage, [(space, sem, loc, builtin), ...]) — только пользовательские
    varying'и (BuiltIn отброшены: gl_Position/SV_Target location не несут)."""
    txt, err = disasm(spv)
    if txt is None:
        return None, err
    i = parse_iface(txt)
    out = []
    for space, key in (("in", "in"), ("out", "out")):
        for v in i[key]:
            if v["builtin"]:
                continue
            out.append((space, v["sem"], v["loc"], v["name"]))
    return (i["stage"], out), None


def remap_locations(spv, locmap, target="vulkan1.0"):
    """Переписывает OpDecorate ... Location N по канонической карте.

    Возвращает (новые_байты, [(имя_переменной, семантика, было, стало)]).
    Декорации BuiltIn и SV_* не трогаются: SV_TARGET — номер цветового
    приложения, а не varying.
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: " + err
    names, builtins = {}, set()
    for line in txt.split("\n"):
        m = RE_NAME.match(line)
        if m:
            names[m.group(1)] = m.group(2)
            continue
        m = RE_BUILTIN.match(line)
        if m:
            builtins.add(m.group(1))
    changed = []
    out = []
    for line in txt.split("\n"):
        m = RE_LOC.match(line)
        if m:
            var, old, tail = m.group(2), int(m.group(4)), m.group(5)
            raw = names.get(var, var)
            sem = split_sem(raw)[2]
            if sem.startswith("SV_") or var in builtins:
                out.append(line)
                continue
            if sem in locmap and locmap[sem] != old:
                line = "%s%s%s%d%s" % (m.group(1), var, m.group(3),
                                       locmap[sem], tail)
                changed.append((raw, sem, old, locmap[sem]))
        out.append(line)
    new, err = assemble("\n".join(out), target)
    if new is None:
        return None, "assemble: " + err
    return new, changed


def check_collisions(spv, label=""):
    """После перестановки: в одном пространстве не должно двух семантик на
    одном location. Пространства: входы VS (вершинные атрибуты), выходы VS =
    входы PS, выходы PS (цветовые приложения)."""
    r, err = interface_vars(spv)
    if r is None:
        return [err]
    stage, vars_ = r
    seen = {}
    bad = []
    for space, sem, loc, raw in vars_:
        if loc is None:
            bad.append("%s: %s без Location" % (label, raw))
            continue
        k = (space, loc)
        if k in seen:
            bad.append("%s: %s и %s оба на %s Location %d"
                       % (label, seen[k], sem, space, loc))
        seen[k] = sem
    return bad


OP_MATRIX_TIMES_MATRIX = 146
OP_TYPE_VECTOR = 23
OP_TYPE_MATRIX = 24






def count_matrix_mult(spv):
    """Сколько OpVectorTimesMatrix / OpMatrixTimesVector в модуле (для логов)."""
    if len(spv) % 4 or len(spv) < 20 or spv[:4] != struct.pack("<I", SPIRV_MAGIC):
        return 0, 0
    w = struct.unpack("<%dI" % (len(spv) // 4), spv)
    i, vt, mt = 5, 0, 0
    while i < len(w):
        wc = w[i] >> 16
        op = w[i] & 0xFFFF
        if wc == 0 or i + wc > len(w):
            break
        if op == OP_VECTOR_TIMES_MATRIX:
            vt += 1
        elif op == OP_MATRIX_TIMES_VECTOR:
            mt += 1
        i += wc
    return vt, mt


RE_TVEC = re.compile(r"^\s*%(\S+)\s*=\s*OpTypeVector\s+%\S+\s+(\d+)")
RE_TMAT = re.compile(r"^\s*%(\S+)\s*=\s*OpTypeMatrix\s+(%\S+)\s+(\d+)")
RE_DEF = re.compile(r"^\s*%(\S+)\s*=\s*(Op\w+)\s+(%\S+)")
RE_MATMUL = re.compile(r"^(\s*)%(\S+)\s*=\s*(OpVectorTimesMatrix|OpMatrixTimesVector"
                       r"|OpMatrixTimesMatrix)\s+(%\S+)\s+(%\S+)\s+(%\S+)\s*$")


def _flip_via_text(spv):
    """Перестановка операндов со вставкой OpTranspose для неквадратных матриц.

    VTM(v_R, M[RxC]) -> MTV(transpose(M), v_R); MTV(M[RxC], v_C) -> VTM(v_C,
    transpose(M)); MTM(A,B) -> MTM(B,A) (типы согласуются сами: у обеих
    матриц размерности тоже транспонированы). Тип transpose(M) =
    OpTypeMatrix <вектор размера C> <R>; нужный векторный тип уже объявлен —
    это тип результата (первый случай) или тип операнда-вектора (второй).
    Объявления типов вставляются ДО первого OpFunction (в теле — запрещены).

    Размерности матрицы-операнда берутся не из имени операнда (это id значения
    из OpLoad/OpAccessChain), а через цепочку «значение -> тип»: сначала
    собираются все OpType*, потом для каждой '%v = Op... %T ...' где %T — тип,
    запоминается тип значения. Возвращает (блоб, число_замен, ошибку).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, 0, "disasm: " + err
    lines = txt.split("\n")
    vec, mat, vtype, vdef = {}, {}, {}, {}
    for line in lines:
        m = RE_DEF.match(line)
        if m:
            vdef["%" + m.group(1)] = m.group(2)
        m = RE_TVEC.match(line)
        if m:
            vec["%" + m.group(1)] = int(m.group(2))
            continue
        m = RE_TMAT.match(line)
        if m:
            mat["%" + m.group(1)] = (vec.get(m.group(2), 0), int(m.group(3)))
            continue
        m = RE_DEF.match(line)
        if m and m.group(3) in vec or (m and m.group(3) in mat):
            vtype["%" + m.group(1)] = m.group(3)
    newtypes, changed, skipped, seq = [], 0, 0, 0
    out = []
    for line in lines:
        m = RE_MATMUL.match(line)
        if not m:
            out.append(line)
            continue
        indent, res, op, rtype, a1, a2 = (m.group(1), m.group(2), m.group(3),
                                         m.group(4), m.group(5), m.group(6))
        # Правим ТОЛЬКО матрицы, загруженные из constant-буфера. Матрицу,
        # собранную в самом шейдере (OpCompositeConstruct — tangent-to-world у
        # normal mapping, UV-матрицы из векторов), оба компилятора трактуют
        # одинаково: её транспонирование ломает освещение (замер 2026-10-17:
        # после сплошной перестановки кадр стал чёрным при PS, залитом
        # константой, — геометрия на месте, цвет чёрный).
        if op == "OpMatrixTimesMatrix":
            if vdef.get(a1) != "OpLoad" or vdef.get(a2) != "OpLoad":
                skipped += 1
                out.append(line)
                continue
            out.append("%s%%%s = %s %s %s %s" % (indent, res, op, rtype, a2, a1))
            changed += 1
            continue
        matid = a2 if op == "OpVectorTimesMatrix" else a1
        if vdef.get(matid) != "OpLoad":
            skipped += 1
            out.append(line)
            continue
        rows, cols = mat.get(vtype.get(matid, ""), (0, 0))
        if not rows or not cols:
            out.append(line)          # размерности неизвестны — не трогаем
            continue
        if rows == cols:
            # операнды меняются местами в обоих случаях: VTM(v,M)->MTV(M,v),
            # MTV(M,v)->VTM(v,M)
            newop = ("OpMatrixTimesVector" if op == "OpVectorTimesMatrix"
                     else "OpVectorTimesMatrix")
            out.append("%s%%%s = %s %s %s %s" % (indent, res, newop, rtype, a2, a1))
            changed += 1
            continue
        # transpose(M) = C строк x R столбцов -> тип колонки = вектор размера C:
        # для VTM(v_R, M) это тип результата, для MTV(M, v_C) — тип операнда-v
        colvec = rtype if op == "OpVectorTimesMatrix" else vtype.get(a2, "")
        if colvec not in vec:
            out.append(line)              # тип колонки неизвестен — не правим
            continue
        seq += 1
        tname = "%%tmat%d" % seq
        newtypes.append("%s = OpTypeMatrix %s %d" % (tname, colvec, rows))
        newop = ("OpMatrixTimesVector" if op == "OpVectorTimesMatrix"
                 else "OpVectorTimesMatrix")
        # множитель — ЗНАЧЕНИЕ OpTranspose (%trN), а не тип (%tmatN)
        trval = "%%tr%d" % seq
        out.append("%s%s = OpTranspose %s %s" % (indent, trval, tname, matid))
        if op == "OpVectorTimesMatrix":      # MTV(transpose(M), v)
            out.append("%s%%%s = %s %s %s %s" % (indent, res, newop, rtype, trval, a1))
        else:                                # VTM(v, transpose(M))
            out.append("%s%%%s = %s %s %s %s" % (indent, res, newop, rtype, a2, trval))
        changed += 1
    if not changed:
        return spv, 0, ""
    body, inserted = [], False
    for line in out:
        if not inserted and re.search(r"=\s*OpFunction\b", line):
            body.extend(newtypes)
            inserted = True
        body.append(line)
    if newtypes and not inserted:
        return None, 0, "нет OpFunction — некуда вставить объявления типов"
    new, err = assemble("\n".join(body))
    if new is None:
        return None, 0, "assemble: " + err
    return new, changed, ""


def fix_matrix_mult(spv):
    """Меняет местами операнды во ВСЕХ матричных умножениях (VTM<->MTV).

    Диагноз (прогоны 56/57; проверено обоими компиляторами на паре
    /tmp/mx/a.hlsl = mul(v,M) и /tmp/mx/b.hlsl = mul(M,v), один и тот же
    исходник, флаги как у драйвера: fxc vs_3_0 flags 0x400, DXC -spirv -Zpr):

      mul(v, M):  fxc  dp4 o0.x, v0, c0/c1/c2/c3  -> result[i]=sum_j v[j]*C_i[j]
                  DXC  OpMatrixTimesVector(M, v)  -> result[r]=sum_c C_c[r]*v[c] ✗
      mul(M, v):  fxc  mad o0, c3, v0.w, ...      -> result[r]=sum_c C_c[r]*v[c]
                  DXC  OpVectorTimesMatrix(v, M)  -> result[c]=sum_r v[r]*C_c[r] ✗

    (C_k = 16-байтовый чанк банка по смещению reg*16. Подтверждено на реальном
    корпусе: ParticleFXMaterial #4 vs — dp4 o3.x, r0, c8/c9/c10/c11 при
    OpMatrixTimesVector в SPIR-V; UIFlashMaterial #0 vs — mad o0, c7, r0.w, ...
    при OpVectorTimesMatrix.)

    Т.е. логическая матрица в SPIR-V у DXC транспонирована к той, что
    подразумевает DXBC; движок грузит в c-регистры транспонированные матрицы
    (стандартная практика D3D9), поэтому DXBC даёт верный кадр, а SPIR-V —
    нет. Разница — ровно транспозиция: (v*Mf)^T = Ms*v, (Mf*v)^T = v*Ms,
    значит лечится перестановкой операндов. Замер корпуса: 252 MTV + 60 VTM
    (137 пар live_indices-2026-10-15-union.json), OpMatrixTimesMatrix нет;
    неквадратных (float4x3 — UV-матрицы) — 35 блоков, там перестановка требует
    явного OpTranspose и нового OpTypeMatrix.

    Возвращает (новый_блоб, число_замен); при провале — (исходный, 0).
    """
    if len(spv) % 4 or len(spv) < 20 or spv[:4] != struct.pack("<I", SPIRV_MAGIC):
        return spv, 0
    new, n, _err = _flip_via_text(spv)
    if new is not None and n:
        return new, n
    return spv, 0




if __name__ == "__main__":
    p = sys.argv[1]
    r, err = interface_vars(open(p, "rb").read())
    if r is None:
        raise SystemExit(err)
    stage, vars_ = r
    print("%s: %s" % (p, stage))
    for space, sem, loc, raw in vars_:
        print("  %-3s %-12s loc=%s  (%s)" % (space, sem, loc, raw))


def force_ps_color(spv, rgba=(1.0, 0.0, 0.0, 1.0), target="vulkan1.0"):
    """Отладочный прогон: заменить ВСЕ записи в цветовые выходы PS константой.

    Интерфейс (число/типы входов, location'ы) не трогается — меняется только
    значение, которое PS пишет в SV_TARGET. Это разделяет «геометрия не доходит
    до растра» (кадр всё равно пустой) и «геометрия есть, но PS даёт чёрный
    цвет» (кадр становится однотонным по форме сцены).

    Возвращает (байты, число_заменённых_записей); при невозможности — (None,
    причина).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: %s" % err
    lines = txt.splitlines()
    # тип float4 (в DXC-листинге имена типов канонические)
    v4 = None
    ftype = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeVector" in t:
            k = t.index("OpTypeVector")
            if len(t) > k + 2 and t[k + 1] == "%float" and t[k + 2] == "4":
                v4, ftype = t[0], t[k + 1]
                break
    if not v4:
        return None, "нет OpTypeVector %float 4"
    # переменные выхода
    outs = set()
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t and t[t.index("OpVariable") + 2] == "Output":
            outs.add(t[0])
    if not outs:
        return None, "нет Output-переменных"
    cname = "_dbg_force_color"
    n = 0
    out = []
    for l in lines:
        t = l.split()
        if len(t) >= 3 and t[0] == "OpStore" and t[1] in outs:
            out.append("     OpStore %s %%%s" % (t[1], cname))
            n += 1
        else:
            out.append(l)
    if not n:
        return None, "нет OpStore в выходы"
    # константа должна быть в глобальной секции — перед первой OpFunction
    # имена в листинге вида «%38 = OpConstant ...» — искать токен, а не начало
    consts = [i for i, l in enumerate(out) if "OpConstant" in l.split()]
    func = [i for i, l in enumerate(out) if "OpFunction" in l.split()]
    # валидная секция: типы идут ПОСЛЕ ранних констант, поэтому вставка —
    # прямо перед первой OpFunction (глобальная секция это допускает)
    pos = func[0] if func else ((max(consts) + 1) if consts else len(out))
    # композитная константа: сначала скаляры, потом OpConstantComposite
    sc, refs = [], []
    for i, v in enumerate(rgba):
        nm = "_dbg_fc%d" % i
        sc.append("%%%s = OpConstant %s %g" % (nm, ftype, v))
        refs.append("%%%s" % nm)
    out.insert(pos, "%%%s = OpConstantComposite %s %s" % (cname, v4, " ".join(refs)))
    for j, l in enumerate(sc):
        out.insert(pos + j, l)
    new, err = assemble("\n".join(out) + "\n", target)
    if new is None:
        return None, "assemble: %s" % err
    return new, n


def force_ps_sample(spv, which="last", target="vulkan1.0"):
    """Отладочный прогон: PS пишет цвет ПЕРВОГО (или ПОСЛЕДНЕГО) выборки из
    текстуры. Разделяет «текстуры чёрные/не те» (кадр показывает картинку
    текстур) и «текстуры в порядке, а чёрный даёт освещение/константы»
    (кадр остаётся чёрным).

    Берутся только OpImageSample*/OpImageFetch* с результатом float4 — иначе
    нужен приведения типа, а цель эксперимента — минимум правок.
    Возвращает (байты, число_замен_или_причина).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: %s" % err
    lines = txt.split("\n")
    v4 = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeVector" in t:
            k = t.index("OpTypeVector")
            if len(t) > k + 2 and t[k + 1] == "%float" and t[k + 2] == "4":
                v4 = t[0]
                break
    if not v4:
        return None, "нет OpTypeVector %float 4"
    SAMPLE_OPS = ("OpImageSampleImplicitLod", "OpImageSampleExplicitLod",
                  "OpImageSampleDrefImplicitLod", "OpImageSampleDrefExplicitLod",
                  "OpImageFetch", "OpImageGather", "OpImageSample")
    samples = []
    for l in lines:
        t = [x for x in l.split() if x != "="]
        ops = [i for i, x in enumerate(t) if x in SAMPLE_OPS]
        for i in ops:
            if len(t) > i + 2 and t[i + 1] == v4:
                samples.append(t[0])      # id результата (в листинге он первым)
                break
    if not samples:
        return None, "нет выборки float4"
    val = samples[-1] if which == "last" else samples[0]
    outs = set()
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t and t[t.index("OpVariable") + 2] == "Output":
            outs.add(t[0])
    n = 0
    out = []
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if len(t) >= 3 and t[0] == "OpStore" and t[1] in outs:
            out.append("%sOpStore %s %s" % (l[:len(l) - len(l.lstrip())], t[1], val))
            n += 1
        else:
            out.append(l)
    if not n:
        return None, "нет OpStore в выходы"
    new, err = assemble("\n".join(out) + "\n", target)
    if new is None:
        return None, "assemble: %s" % err
    return new, n


def force_ps_varying(spv, which=0, target="vulkan1.0"):
    """Отладочный прогон: PS пишет ПЕРВЫЙ (which-й) входной varying.

    Разделяет «интерфейс VS->PS сломан (varying нулевой)» (кадр чёрный) и
    «varying в порядке, а чёрный даёт математика PS» (кадр цветной).
    Тип varyingа — любой вектор из float (в PS корпуса это float2/float3/float4);
    к выходу приводится OpCompositeExtract + OpCompositeConstruct (alpha = 1).
    OpLoad вставляется сразу после первого OpLabel. Возвращает (байты, пояснение).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: %s" % err
    lines = txt.split("\n")
    # векторы float -> (тип, число компонент), и имя float-типа
    ftype, vec = None, {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeFloat" in t:
            k = t.index("OpTypeFloat")
            if len(t) > k + 1 and t[k + 1] == "32":
                ftype = t[0]
        if "OpTypeVector" in t and ftype:
            k = t.index("OpTypeVector")
            if len(t) > k + 2 and t[k + 1] == ftype:
                vec[t[0]] = int(t[k + 2])
    v4 = next((k for k, n in vec.items() if n == 4), None)
    if not v4:
        return None, "нет OpTypeVector %float 4"
    # OpVariable указывает на OpTypePointer, а не на тип значения напрямую
    ptr2val = {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypePointer" in t:
            k = t.index("OpTypePointer")
            if len(t) > k + 2:
                ptr2val[t[0]] = (t[k + 1], t[k + 2])
    ins = []
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t:
            k = t.index("OpVariable")
            if len(t) > k + 2:
                tv = ptr2val.get(t[k + 1])
                if tv and tv[0] == "Input" and tv[1] in vec:
                    ins.append((t[0], tv[1]))
    if not ins:
        return None, "нет входного varying из float"
    src, styp = ins[min(which, len(ins) - 1)]
    outs = set()
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t:
            k = t.index("OpVariable")
            tv = ptr2val.get(t[k + 1]) if len(t) > k + 1 else None
            if tv and tv[0] == "Output":
                outs.add(t[0])
    if not outs:
        return None, "нет выходной переменной"
    funcl = next((i for i, l in enumerate(lines) if re.search(r"=\s*OpFunction\b", l)),
                 len(lines))
    out = (lines[:funcl] + ["%%ptrin%d = OpTypePointer Input %s" % (which, styp)]
           + lines[funcl:])
    res = []
    loaded = False
    for l in out:
        res.append(l)
        if not loaded and re.search(r"=\s*OpLabel\b", l):
            ind = l[:len(l) - len(l.lstrip())]
            res.append("%s%%vpv = OpLoad %s %s" % (ind, styp, src))
            for c in range(vec[styp]):
                res.append("%s%%vpc%d = OpCompositeExtract %s %%vpv %d"
                           % (ind, c, ftype, c))
            comps = ["%%vpc%d" % c for c in range(min(3, vec[styp]))]
            while len(comps) < 3:
                comps.append("%%vpc%d" % (vec[styp] - 1))
            res.append("%s%%vp4 = OpCompositeConstruct %s %s %%float_1"
                       % (ind, v4, " ".join(comps)))
            loaded = True
    if not loaded:
        return None, "нет OpLabel"
    # константа 1.0 (если в модуле её нет — добавим в глобальную секцию)
    one = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpConstant" in t:
            k = t.index("OpConstant")        # id результата идёт первым токеном
            if len(t) > k + 2 and t[k + 1] == ftype:
                try:
                    if abs(float(t[k + 2].rstrip("f"))) == 1.0:
                        one = t[0]
                except ValueError:
                    pass
    if one is None:
        one = "%float_1"
        pos = next((i for i, l in enumerate(res) if re.search(r"=\s*OpFunction\b", l)),
                   len(res))
        res.insert(pos, "%s = OpConstant %s 1.0" % (one, ftype))
    final = []
    n = 0
    for l in res:
        t = [x for x in l.split() if x != "="]
        if len(t) >= 3 and t[0] == "OpStore" and t[1] in outs:
            final.append("%sOpStore %s %%vp4" % (l[:len(l) - len(l.lstrip())], t[1]))
            n += 1
        else:
            final.append(l)
    new, err = assemble("\n".join(final) + "\n", target)
    if new is None:
        return None, "assemble: %s" % err
    return new, "%d выходов <- %s:%s (входов %d)" % (n, src, styp, len(ins))


def force_ps_id(spv, spec="last:fadd", target="vulkan1.0"):
    """Отладочный прогон: PS пишет значение ВЫБРАННОГО SSA-значения.

    spec: "%123" — явный id; либо "<op>:N", где op в
    dot|fmul|fadd|fsub|fmuladd|sample|load|ext|shuffle|select|ternary,
    а N — номер по порядку в листинге (0-based, отрицательный — с конца).
    Разделяет стадии PS-арифметики: если «освещение» (последний fadd перед
    записью) даёт цвет — чёрный появляется позже, если чёрное — дефект выше
    по потоку данных. Значения float/float2/float3 приводятся к float4
    (недостающие компоненты — повтором последней, alpha = 1).
    Возвращает (байты, пояснение_или_причина).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: %s" % err
    lines = txt.split("\n")
    ftype = None
    vec = {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeFloat" in t and t[t.index("OpTypeFloat") + 1] == "32":
            ftype = t[0]
        if "OpTypeVector" in t and ftype:
            k = t.index("OpTypeVector")
            if len(t) > k + 2 and t[k + 1] == ftype:
                vec[t[0]] = int(t[k + 2])
    v4 = next((k for k, n in vec.items() if n == 4), None)
    if not v4:
        return None, "нет OpTypeVector %float 4"
    # значение -> тип (первый операнд строки определения — тип результата)
    vtype = {}
    for l in lines:
        m = RE_DEF.match(l)
        if m and (m.group(3) == ftype or m.group(3) in vec):
            vtype["%" + m.group(1)] = m.group(3)
    OPS = {"dot": ["OpDot"],
           "fmul": ["OpFMul"], "fadd": ["OpFAdd"], "fsub": ["OpFSub"],
           "fmuladd": ["OpFMod"], "ext": ["OpExtInst"],
           "shuffle": ["OpVectorShuffle"], "select": ["OpSelect"],
           "load": ["OpLoad"], "composite": ["OpCompositeConstruct"],
           "sample": ["OpImageSampleImplicitLod", "OpImageSampleExplicitLod",
                      "OpImageSampleDrefImplicitLod", "OpImageSampleDrefExplicitLod",
                      "OpImageFetch", "OpImageGather"]}
    cands = []
    if ":" in spec:
        kind, num = spec.split(":", 1)
        ops = OPS.get(kind.strip())
        if not ops:
            return None, "неизвестный класс значений: %s" % kind
        for l in lines:
            t = [x for x in l.split() if x != "="]
            hit = [i for i, x in enumerate(t) if x in ops]
            for i in hit:
                # тип результата — float/вектор из float (id типа в vtype не лежит)
                if len(t) > i + 1 and (t[i + 1] == ftype or t[i + 1] in vec) and t[0].startswith("%"):
                    cands.append(t[0])
                    break
        if not cands:
            return None, "нет значений класса %s" % kind
        k = int(num)
        val = cands[k if k >= 0 else len(cands) + k]
    else:
        val = spec if spec.startswith("%") else "%" + spec
        if val not in vtype:
            return None, "id %s не найден среди float-значений" % val
    vt = vtype[val]
    ptr2val = {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypePointer" in t:
            k = t.index("OpTypePointer")
            if len(t) > k + 2:
                ptr2val[t[0]] = (t[k + 1], t[k + 2])
    outs = set()
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t:
            tv = ptr2val.get(t[t.index("OpVariable") + 1])
            if tv and tv[0] == "Output":
                outs.add(t[0])
    if not outs:
        return None, "нет выходной переменной"
    one = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpConstant" in t:
            k = t.index("OpConstant")
            if len(t) > k + 2 and t[k + 1] == ftype:
                try:
                    if abs(float(t[k + 2].rstrip("f"))) == 1.0:
                        one = t[0]
                except ValueError:
                    pass
    if one is None:
        one = "%one_c"
    res = []
    loaded = False
    for l in lines:
        res.append(l)
        if loaded:
            continue
        t = l.split()
        if not (len(t) >= 3 and t[0] == val and t[1] == "="):
            continue            # вставка сразу после определения выбранного id
        ind = l[:len(l) - len(l.lstrip())]
        if vt == v4:
            use = val
        elif vt == ftype:
            res.append("%s%%pi4 = OpCompositeConstruct %s %s %s %s %s"
                       % (ind, v4, val, val, val, one))
            use = "%pi4"
        else:
            n = vec[vt]
            for c in range(n):
                res.append("%s%%pid%d = OpCompositeExtract %s %s %d" % (ind, c, ftype, val, c))
            comps = ["%%pid%d" % c for c in range(min(3, n))]
            while len(comps) < 3:
                comps.append("%%pid%d" % (n - 1))
            res.append("%s%%pi4 = OpCompositeConstruct %s %s %s" % (ind, v4, " ".join(comps), one))
            use = "%pi4"
        loaded = True
    if not loaded:
        return None, "определение %s не найдено (строка «%s = Op...»)" % (val, val)
    if one == "%one_c":
        pos = next((i for i, l in enumerate(res) if re.search(r"=\s*OpFunction\b", l)),
                   len(res))
        res.insert(pos, "%s = OpConstant %s 1.0" % (one, ftype))
    final = []
    n = 0
    for l in res:
        t = [x for x in l.split() if x != "="]
        if len(t) >= 3 and t[0] == "OpStore" and t[1] in outs:
            final.append("%sOpStore %s %s" % (l[:len(l) - len(l.lstrip())], t[1], use))
            n += 1
        else:
            final.append(l)
    new, err = assemble("\n".join(final) + "\n", target)
    if new is None:
        return None, "assemble: %s" % err
    ok, verr = validate(new, target)
    if not ok:
        return None, "val: %s" % verr
    return new, "%d выходов <- %s : %s" % (n, val, vt)


def force_ps_bank(spv, which=0, target="vulkan1.0"):
    """Отладочный прогон: PS пишет ЗНАЧЕНИЕ члена c-банка (which-й float4-член
    структуры pwCBankPS/VS, смещение = reg*16).

    Четвёртая проба, закрывает последнюю непроверенную связь: константный банк.
    Пробы «PS = константа», «PS = varying», «PS = выборка текстуры» цветные, а
    полный PS даёт чёрный -> подозрение, что UBO констант до шейдера не доходит
    (нули). Если кадр с этой пробой ЦВЕТНОЙ (для BasicMaterial первый float4-член
    = ambientColor 0.6,0.6,0.6 -> серый кадр), банк в порядке и виновата
    арифметика PS; если чёрный — банк не читается.
    Возвращает (байты, пояснение).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: %s" % err
    lines = txt.split("\n")
    ftype, vec, uint = None, {}, None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeFloat" in t:
            k = t.index("OpTypeFloat")
            if len(t) > k + 1 and t[k + 1] == "32":
                ftype = t[0]
        if "OpTypeInt" in t and uint is None:
            k = t.index("OpTypeInt")
            if len(t) > k + 2 and t[k + 1] == "32" and t[k + 2] == "0":
                uint = t[0]
        if "OpTypeVector" in t and ftype:
            k = t.index("OpTypeVector")
            if len(t) > k + 2 and t[k + 1] == ftype:
                vec[t[0]] = int(t[k + 2])
    v4 = next((k for k, n in vec.items() if n == 4), None)
    if not v4 or uint is None:
        return None, "нет float4 или uint"
    # имя структуры банка -> id
    name_of = {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpName" in t:
            k = t.index("OpName")
            if len(t) > k + 2:
                name_of.setdefault(t[k + 1], t[k + 2].strip('"'))
    structs = {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeStruct" in t:
            k = t.index("OpTypeStruct")
            structs[t[0]] = t[k + 1:]
    ptr2val = {}
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypePointer" in t:
            k = t.index("OpTypePointer")
            if len(t) > k + 2:
                ptr2val[t[0]] = (t[k + 1], t[k + 2])
    # банк = структура у Uniform-переменной. Имя разное: в корпусе со швом это
    # type_pwCBankVS/PS, у сырого DXC-компилята безымянный cbuffer зовётся
    # type__Globals, так что имя — только предпочтение, а не условие.
    cands = []
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t:
            k = t.index("OpVariable")
            if len(t) > k + 2 and t[k + 2] == "Uniform":
                tv = ptr2val.get(t[k + 1])
                if tv and tv[1] in structs:
                    cands.append((t[0], tv[1]))
    if not cands:
        return None, "нет OpVariable Uniform со структурой"

    def rank(cv):
        nm = name_of.get(cv[1], "")
        return (0 if "CBank" in nm else 1 if "Globals" in nm else 2,
                -len(structs[cv[1]]))
    var, sid = sorted(cands, key=rank)[0]
    members = structs[sid]
    bname = name_of.get(sid, "#%s" % sid)
    # which-й член заданного типа (float4), по индексу члена
    want = [i for i, m in enumerate(members) if m == v4]
    if not want:
        return None, "в %s нет float4-членов" % bname
    midx = want[min(which, len(want) - 1)]
    outs = set()
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t:
            k = t.index("OpVariable")
            tv = ptr2val.get(t[k + 1]) if len(t) > k + 1 else None
            # только float4-выходы: в VS есть float2/float3 выходы, в них
            # float4 писать нельзя (несовпадение типа OpStore)
            if tv and tv[0] == "Output" and tv[1] == v4:
                outs.add(t[0])
    if not outs:
        return None, "нет float4-выходной переменной"
    funcl = next((i for i, l in enumerate(lines) if re.search(r"=\s*OpFunction\b", l)),
                 len(lines))
    glob = ["%%_dbg_bpt = OpTypePointer Uniform %s" % v4,
            "%%_dbg_bidx = OpConstant %s %d" % (uint, midx)]
    out = lines[:funcl] + glob + lines[funcl:]
    res, loaded = [], False
    for l in out:
        res.append(l)
        if not loaded and re.search(r"=\s*OpLabel\b", l):
            ind = l[:len(l) - len(l.lstrip())]
            res.append("%s%%dbg_bac = OpAccessChain %%_dbg_bpt %s %%_dbg_bidx"
                       % (ind, var))
            res.append("%s%%dbg_bv = OpLoad %s %%dbg_bac" % (ind, v4))
            loaded = True
    if not loaded:
        return None, "нет OpLabel"
    final, n = [], 0
    for l in res:
        t = [x for x in l.split() if x != "="]
        if len(t) >= 3 and t[0] == "OpStore" and t[1] in outs:
            final.append("%sOpStore %s %%dbg_bv" % (l[:len(l) - len(l.lstrip())], t[1]))
            n += 1
        else:
            final.append(l)
    new, err = assemble("\n".join(final) + "\n", target)
    if err or new is None:
        return None, "assemble: %s" % err
    off = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpMemberDecorate" in t:
            k = t.index("OpMemberDecorate")
            if len(t) > k + 3 and t[k + 1] == sid and t[k + 2] == str(midx) and t[k + 3] == "Offset":
                off = t[k + 4]
    return new, "%d выходов <- %s[%d] (c%s, Offset %s)" % (n, bname, midx,
                                                           (int(off) // 16) if off else "?", off)


def force_ps_sample_at(spv, uv=(0.5, 0.5), which="first", target="vulkan1.0"):
    """Отладка (2026-10-18): PS пишет выборку из текстуры, но координата
    ЗАМЕНЕНА на константу. Снимает с проверки UV: если выборка в фиксированной
    точке тоже чёрная — чёрная сама текстура на GPU (или привязан не тот
    объект); если цветная — сломан путь координат.

    Координата — 5-й операнд OpImageSample* (после типа, результата, образа и
    сэмплера); её тип берётся из определяющей строки (второй операнд команды).
    Возвращает (байты, число_замен_или_причина).
    """
    txt, err = disasm(spv)
    if txt is None:
        return None, "disasm: %s" % err
    lines = txt.split("\n")
    v4 = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpTypeVector" in t:
            k = t.index("OpTypeVector")
            if len(t) > k + 2 and t[k + 1] == "%float" and t[k + 2] == "4":
                v4 = t[0]
                break
    if not v4:
        return None, "нет OpTypeVector %float 4"
    SAMPLE_OPS = ("OpImageSampleImplicitLod", "OpImageSampleExplicitLod",
                  "OpImageSampleDrefImplicitLod", "OpImageSampleDrefExplicitLod",
                  "OpImageFetch")
    # (id результата, id координаты)
    cands = []
    for l in lines:
        t = [x for x in l.split() if x != "="]
        for i, x in enumerate(t):
            # листинг spirv-dis: id результата ПЕРВЫМ токеном, дальше опкод,
            # тип выборки, образ (OpSampledImage: образ+сэмплер уже слиты),
            # координата, затем операнд памяти (None)
            if x in SAMPLE_OPS and len(t) > i + 3 and t[i + 1] == v4:
                cands.append((t[0], t[i + 3]))
                break
    if not cands:
        return None, "нет выборки float4"
    res, coord = cands[-1] if which == "last" else cands[0]
    # тип координаты — из строки, где она определена
    ctype = None
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if len(t) > 2 and t[0] == coord:
            ctype = t[2]
            break
    if not ctype:
        return None, "не найден тип координаты %s" % coord
    outs = set()
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if "OpVariable" in t and "Output" in t:
            outs.add(t[0])
    if not outs:
        return None, "нет Output-переменной"
    # компоненты константы координаты по типу координаты
    if ctype == "%float":
        newc, cins = "%_dbg_u0", ["%%_dbg_u0 = OpConstant %%float %s" % uv[0]]
    else:
        comps = ["%_dbg_u0", "%_dbg_u1"]
        if ctype == "%v3float":
            comps.append("%_dbg_u2")
            cins = ["%%_dbg_u0 = OpConstant %%float %s" % uv[0],
                    "%%_dbg_u1 = OpConstant %%float %s" % uv[1],
                    "%%_dbg_u2 = OpConstant %%float 0.0"]
        else:
            cins = ["%%_dbg_u0 = OpConstant %%float %s" % uv[0],
                    "%%_dbg_u1 = OpConstant %%float %s" % uv[1]]
        newc = "%_dbg_uv"
        cins.append("%%_dbg_uv = OpConstantComposite %s %s" % (ctype, " ".join(comps)))
    out = []
    n = 0
    done = False
    for l in lines:
        t = [x for x in l.split() if x != "="]
        if t and t[0] == res and any(x in SAMPLE_OPS for x in t):
            l = l.replace(" " + coord + " ", " " + newc + " ", 1)
            n += 1
        out.append(l)
        if not done and re.search(r"=\s*OpFunction\b", l):
            out[-1] = "\n".join(cins + [out[-1]])
            done = True
    if not n:
        return None, "выбранная выборка не найдена в теле (%s/%s)" % (res, coord)
    blob, err = assemble("\n".join(out), target)
    if err:
        return None, "assemble: %s" % err
    return blob, "%d выборок <- UV=(%s,%s) тип %s" % (n, uv[0], uv[1], ctype)
