#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cbank.py — канонический c-bank UBO (шаг 12b очереди этапа 3).

Зачем. §3.z п.2 предполагал, что писатель строит из SPIR-V таблицу
«const-регистр -> байт UBO» и носит её в контейнере. Альтернатива дешевле и
прочнее: заставить САМОЙ раскладки UBO совпадать с раскладкой c-банка D3D9,
т.е. `byte = 16 * reg`. Тогда таблиц нет вообще: клиент отдаёт тень констант
(`ConstBlock` шага 7) как есть, бэкенд грузит диапазон по формуле, и layout
одинаков для всех материалов (один UBO на VS и один на PS).

Механизм — `packoffset(cN)` (замер 2026-10-15, DXC 2025):
  cbuffer pwC : register(b0) {
      float4x4 World  : packoffset(c0);   -> Offset 0,   MatrixStride 16, RowMajor
      float3   amb    : packoffset(c4);   -> Offset 64
      float4   arr[9] : packoffset(c21);  -> Offset 336, ArrayStride 16
      float    sc     : packoffset(c30);  -> Offset 480
  };
т.е. смещение = 16*номер регистра ровно как в D3D9. Важные ограничения замера:
  * `pack_offset(...)` — НЕ валидно, только `packoffset(...)`;
  * наложение двух членов DXC запрещает («packoffset caused overlap with
    previous members»), тогда как fxc/SM3 перекрывающиеся `register()` терпят
    (неиспользуемые регистры просто не выделяются) — значит банк обязан
    содержать ТОЛЬКО объявления, дошедшие до данной точки входа;
  * несколько блоков `cbuffer pwC : register(b0)` в одном компиляторском
    прогоне (в т.ч. из разных include'ов) DXC НЕ сливает: получается несколько
    переменных на одном set/binding (битый SPIR-V). Поэтому банк — ОДИН блок на
    компиляцию, генерируемый здесь, а исходные объявления в файлах гасятся.

Отсюда две задачи этого модуля: (1) разбор `GlobalMasks.h` (имя макроса ->
номер регистра), (2) замыкание include'ов от точки входа материала и сборка
банка без конфликтов.
"""

import os
import re
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_R1117 = os.path.abspath(os.path.join(_HERE, "..", "..", "pw", "branches", "r1117"))
SH = os.path.join(_R1117, "Src", "Render", "Shaders")
MASKS = os.path.join(_R1117, "Src", "Render", "GlobalMasks.h")

# объявление константы: [const|uniform|static|extern] ТИП имя[массив] : register( МАКРОС|cN ) ;
# `uniform` в альтернативе обязателен: без него объявления _commonUI.hlsl /
# UIFlashMaterial.hlsl (20 шт.) терялись и материал терял членов банка
# (замер 2026-10-18: «членов на пару макс 3» вместо 20, HUD не рисовался).
DECL = re.compile(
    r"^[ \t]*(?P<const>const\s+|uniform\s+|static\s+|extern\s+)?(?P<type>float4x4|float4x3|float4x2|float3x4|"
    r"float3x3|float2x4|float2x2|matrix|float4|float3|float2|float|half4|half3|"
    r"half2|half|int4|int3|int2|int|bool4|bool3|bool2|bool|uint4|uint3|uint2|uint)"
    r"[ \t]+(?P<name>\w+)[ \t]*(?P<arr>\[[^]]*\])?[ \t]*:[ \t]*register[ \t]*"
    r"\([ \t]*(?P<reg>c[ \t]*\d+|[A-Z_][A-Z0-9_]*)[ \t]*\)[ \t]*;",
    re.M)

INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)

# определение функции верхнего уровня: ТИП имя(параметры) [: семантика] {
FUNC = re.compile(
    r"^[ \t]*(?P<ret>[A-Za-z_]\w*)[ \t]+(?P<name>\w+)[ \t]*\((?P<params>[^;{}]*?)\)"
    r"[ \t]*(?::[ \t]*\w+)?[ \t\r\n]*\{", re.M)
NOT_FUNC = {"if", "for", "while", "switch", "return", "else", "do", "struct",
            "cbuffer", "typedef", "define", "if_n", "for_n"}


def read(path):
    return open(path, encoding="utf-8", errors="replace").read()


def mask_map():
    """GlobalMasks.h -> {ИМЯ: номер регистра} и порядок секций для диагностики.

    Макросы вида `#define WORLD c0` (есть и `#define X s10` — сэмплеры, их не
    берём). Значение может быть `c24` или числом без `c`.
    """
    out = {}
    for line in read(MASKS).splitlines():
        m = re.match(r"\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+c\s*(\d+)\b", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def num_map():
    """GlobalMasks.h -> {ИМЯ: число} для размеров массивов (`[2 * POINTLIGHTSCOUNT]`).

    Отдельно от c-регистровых макросов: в GlobalMasks.h вперемешку `#define WORLD
    c0` и `#define POINTLIGHTSCOUNT 4`, а размеры массивов надо вычислять по
    вторым (замер 2026-10-15: `float4 lightData[2 * POINTLIGHTSCOUNT]` без этого
    давал шаг 1 регистр, DXC резервировал 8 и падал на «packoffset overlap»).\"\"\"
    """
    out = {}
    for line in read(MASKS).splitlines():
        m = re.match(r"\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+(\d+)\s*(//.*)?$", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


NUMS = num_map()


def resolve_reg(expr, masks):
    """`c12` или `VIEWPROJECTION` -> 12 / None, если макрос не c-регистровый."""
    m = re.match(r"c\s*(\d+)$", expr)
    if m:
        return int(m.group(1))
    return masks.get(expr)


def type_span(typ, arr, masks):
    """Сколько c-регистров занимает объявление (для проверки перекрытий).

    Матрица = 4 регистра (row-major, stride 16 Б), вектор/скаляр = 1, массив =
    длина * шаг элемента. Длину массива берём из текста (`[9]`) или из макроса
    (`[MAX_INSTANCES_COUNT]`).
    """
    def one():
        if typ in ("float4x4", "float4x3", "float4x2", "float3x4", "float3x3",
                   "float2x4", "float2x2", "matrix"):
            return 4
        return 1
    if not arr:
        return one()
    n = arr[1:-1].strip()
    if n.isdigit():
        return one() * int(n)
    if n in masks:
        return one() * masks[n]
    # арифметика с макросами (`2 * POINTLIGHTSCOUNT`, `(POINTLIGHTSCOUNT + 3)/4`)
    if re.match(r"^[A-Za-z0-9_+*/() -]+$", n):
        names = re.findall(r"[A-Za-z_]\w*", n)
        if all(x in NUMS for x in names):
            try:
                return one() * int(eval(n, {"__builtins__": {}}, NUMS))
            except Exception:
                return None
    return None      # длина неизвестна — перекрытие проверить нельзя


def decls_text(text):
    out = []
    for m in DECL.finditer(text):
        out.append(dict(type=m.group("type"), name=m.group("name"),
                        arr=m.group("arr"), reg=m.group("reg"),
                        start=m.start(), end=m.end()))
    return out


def closure(entry, extra=()):
    """Порядок include-замыкания (сам входной файл первым)."""
    seen, order, stack = set(), [], [entry]
    while stack:
        f = stack.pop(0)
        if f in seen or not os.path.isfile(f):
            continue
        seen.add(f)
        order.append(f)
        for m in INCLUDE.finditer(read(f)):
            # `#include "..\OnlineShaders\_commonDepthRead.h"` — в корпусе есть
            # включы с обратными слэшами, без нормализации файл выпадал из
            # замыкания и его c-объяления не попадали в банк (замер
            # 2026-10-15: DXC падал на «redefinition of 'madValueUV'»)
            inc = m.group(1).replace("\\", "/")
            p = os.path.normpath(os.path.join(os.path.dirname(f), inc))
            if os.path.isfile(p):
                stack.append(p)
            else:
                p2 = os.path.normpath(os.path.join(SH, inc))
                if os.path.isfile(p2):
                    stack.append(p2)
    for f in extra:
        if os.path.isfile(f) and f not in seen:
            order.append(f)
    return order


def bodies(files):
    """{имя функции: тело} по списку файлов."""
    return bodies_text("\n".join(read(f) for f in files))


def bodies_text(text):
    """{имя функции: текст тела} (тело — от { до парного })."""
    out = {}
    for m in FUNC.finditer(text):
        if m.group("name") in NOT_FUNC:
            continue
        i, depth = m.end() - 1, 0
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if not depth:
                    break
            i += 1
        body = text[m.end() - 1:i + 1]
        # тело может быть объявлено дважды (пины/`#if`) — объединяем
        out[m.group("name")] = out.get(m.group("name"), "") + body
    return out


def reachable(entry_name, funcs):
    """Имена функций, доходимых из точки входа (транзитивный замыкание вызовов)."""
    seen, stack = set(), [entry_name]
    while stack:
        n = stack.pop()
        if n in seen or n not in funcs:
            continue
        seen.add(n)
        for name in funcs:
            if name in seen:
                continue
            if re.search(r"\b%s\s*\(" % re.escape(name), funcs[n]):
                stack.append(name)
    return seen


def used_names(entry_name, funcs):
    """Идентификаторы, встречающиеся в телах достижимых функций."""
    text = "\n".join(funcs.get(n, "") for n in reachable(entry_name, funcs))
    return set(re.findall(r"\b[A-Za-z_]\w*\b", text))


def preprocess(entry, defines=()):
    """Текст единицы компиляции после препроцессора DXC (с раскрытием `#if`).

    Нужен затем, чтобы банк строился по ФАКТИЧЕСКИ дошедшим объявлениям: пины
    материала (`INSTANCING` и т.п.) включают/выключают целые ветки, и без
    препроцессора достижимость переоценивает (замер 2026-10-15: BasicMaterial VS
    даёт 6 фиктивных перекрытий `World` c0..c3 против инстансинговых c0..c3).
    """
    import dxcdrv
    out = "/tmp/cbank_pre_%s.txt" % os.path.basename(entry)[:-5]
    cmd = [dxcdrv.DXC, "-P", "-T", "vs_6_0", "-Fi", out]
    for d in defines:
        cmd += ["-D", d]
    cmd += ["-I", os.path.dirname(entry), "-I", cbank_dir(), entry]
    p = subprocess.run(cmd, env=dxcdrv.ENV, capture_output=True, text=True)
    if p.returncode != 0 or not os.path.isfile(out):
        raise SystemExit("препроцесс %s: %s"
                         % (entry, (p.stdout + p.stderr).strip()[:400]))
    return read(out)


def cbank_dir():
    return os.path.join(_R1117, "Src", "Render")


def bank(entry, masks, stage="vs", text=None):
    """Банк одной точки входа: [(reg, type, name, arr, span, файл, live)].

    Два класса членов.

    1. `live` — имя дошло до точки входа. Регистр берётся из исходника
       (макрос GlobalMasks.h), т.е. соседство совпадает с D3D9. Пересечение двух
       live-членов — ошибка: в SM3 таких случаев нет (fxc для неиспользуемого
       имени регистр не выделяет), значит перекрытие — это баг раскладки, его
       надо чинить в исходниках.
    2. `extra` — имя НЕ дошло до точки входа, но упомянуто в теле функции, которая
       осталась в TU. DXC разрешает имена во ВСЕХ телах, даже недостижимых
       (замер 2026-10-15: `screenScaleOffset` не нужен MainPass_VSMain, но его
       читает мёртвая `ScreenToTexture` в `_common.hlsl:52`; после снятия
       объявления DXC падает «use of undeclared identifier»). Такое объявление
       нельзя убирать, но и оставлять на его D3D9-номере нельзя: оно перекрывается
       с live-членами (в SM3 просто не выделялось). Таким членам выдаётся СВОБОДНЫЙ
       номер в хвосте банка; код их не читает, поэтому номер безразличен.
    """
    files = closure(entry)
    if text is None:
        text = "\n".join(read(f) for f in files)
    funcs = bodies_text(text)
    suf = "_VSMain" if stage == "vs" else "_PSMain"
    entry_name = next((n for n in funcs if n.endswith(suf)), "")
    reach_used = used_names(entry_name, funcs)
    decls = decls_text(text)
    stripped = list(text)          # текст без самих объявлений
    for d in decls:
        for i in range(d["start"], d["end"]):
            stripped[i] = " "
    stripped = "".join(stripped)
    live, extra, dropped, dead = [], [], [], []
    seen_member = set()
    for d in decls:
        reg = resolve_reg(d["reg"], masks)
        if reg is None:
            dropped.append((entry, d["name"], d["reg"], "не c-регистр"))
            continue
        is_live = d["name"] in reach_used
        if not is_live and not re.search(r"\b%s\b" % re.escape(d["name"]),
                                         stripped):
            dead.append((entry, d["name"], reg))
            continue
        key = (d["type"], d["name"], d["arr"])
        if key in seen_member:      # одно и то же объявление в `#if`-ветках
            continue
        seen_member.add(key)
        span = type_span(d["type"], d["arr"], masks)
        (live if is_live else extra).append(
            (reg, d["type"], d["name"], d["arr"], span, entry, is_live))
    live.sort(key=lambda x: (x[0], x[2]))

    # хвост для «мёртвых, но названных»: первый свободный номер после live-диапазона
    used_regs = set()
    for m in live:
        for i in range(m[0], m[0] + (m[4] or 1)):
            used_regs.add(i)
    cursor = max([m[0] + (m[4] or 1) for m in live], default=0)
    for m in sorted(extra, key=lambda x: (x[0], x[2])):
        span = m[4] or 1
        while any(r in used_regs for r in range(cursor, cursor + span)):
            cursor += 1
        for r in range(cursor, cursor + span):
            used_regs.add(r)
        m = (cursor,) + m[1:]
        live.append(m)
        cursor += span
    members = live

    conflicts = []
    for i in range(len(members)):
        for j in range(i + 1, len(members)):
            a, b = members[i], members[j]
            if not (a[6] and b[6]):
                continue           # пересечение с «хвостовым» невозможнo по построению
            sa = a[4] or 1
            sb = b[4] or 1
            if a[0] + sa > b[0] and b[0] + sb > a[0]:
                conflicts.append((a, b))
    return members, dead, dropped, conflicts


def bank_block(entry, masks, stage="vs", text=None, breg=None):
    """Текст канонического c-банка + его члены.

    У каждого этапа свой банк (в D3D9 таблица констант вершинного и пиксельного
    этапа независимы), поэтому в наборе дескрипторов это два разных UBO:
    VS -> set 0 binding 0, PS -> set 0 binding 1 (`b0`/`b1`). Номера `c` берутся
    из исходника (макросы GlobalMasks.h), так что побайтовое соседство констант
    совпадает с D3D9; `packoffset` обязателен, иначе DXC раскладывает членов по
    своим правилам и смещения уезжают (замер 2026-10-15: без packoffset
    `float4x4 World` + `float4 amb` дали Offset 0 и 16 вместо 0 и 64).

    Типы-исключения (`float4x3` и т.п.), у которых размер в std140 больше,
    чем `regCount` c-регистров, в живом корпусе не встречаются (замер: 185
    членов — только float4x4/float4/float3/float2/float), поэтому сдвиги
    соседей не считаются; при появлении DXC сам упадёт на пересечении packoffset.
    """
    members, dead, dropped, conflicts = bank(entry, masks, stage, text)
    if conflicts:
        raise SystemExit(
            "%s %s: %d пересечений членов банка — сначала править исходники:\n  %s"
            % (os.path.basename(entry), stage.upper(), len(conflicts),
               "\n  ".join("c%d %s %s <-> c%d %s %s"
                           % (a[0], a[1], a[2], b[0], b[1], b[2])
                           for a, b in conflicts[:6])))
    if breg is None:
        breg = 0 if stage == "vs" else 1
    members = sorted(members, key=lambda x: (x[0], x[2]))
    lines = ["// сгенерировано tools/shader_build/cbank.py — не править вручную",
             "cbuffer pwCBank%s : register(b%d, space0) {"
             % (stage.upper(), breg)]
    for reg, typ, name, arr, span, _f, _live in members:
        lines.append("    %s %s%s : packoffset(c%d);" % (typ, name, arr or "", reg))
    lines.append("};")
    return "\n".join(lines), members, dead, dropped


def main():
    import argparse
    ap = argparse.ArgumentParser(description="обзор канонического c-банка")
    ap.add_argument("--define", action="append", default=[], metavar="ИМЯ=ЗНАЧ",
                    help="макрос препроцессора (повторяемо), напр. PW_MODERN_ARRAYS=1")
    ap.add_argument("--material", help="только этот материал")
    ap.add_argument("--no-pre", action="store_true",
                    help="не гонять препроцессор DXC (консервативная достижимость)")
    ap.add_argument("--max-pins", action="store_true",
                    help="раскрыть все пины материала в maxPinValue (худший случай "
                         "по дошедшим объявлениям)")
    args = ap.parse_args()

    masks = mask_map()
    masks_numeric = {k: v for k, v in masks.items()}
    # отдельная карта для длин массивов ([MAX_BONES] и т.п.)
    for line in read(MASKS).splitlines():
        m = re.match(r"\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+(\d+)\b", line)
        if m and m.group(1) not in masks_numeric:
            masks_numeric[m.group(1)] = int(m.group(2))
    print("GlobalMasks: c-регистровых макросов %d, препроцесс: %s, defines: %s"
          % (len(masks), "нет" if args.no_pre else "DXC -E", args.define or "-"))
    mats = sorted(f[:-5] for f in os.listdir(SH) if f.endswith(".hlsl"))
    if args.material:
        mats = [m for m in mats if m == args.material]
    tot_m = tot_d = tot_c = 0
    pins_of = {}
    if args.max_pins:
        import dxcdrv
        pins_of = dict(dxcdrv.spec_materials())
    for m in sorted(mats):
        entry = os.path.join(SH, m + ".hlsl")
        text = None
        if not args.no_pre:
            defs = list(args.define)
            for pname, mx, _mult in pins_of.get(m, []):
                defs.append("%s=%d" % (pname, mx))
            try:
                text = preprocess(entry, defs)
            except SystemExit as e:
                print("%-30s пропуск: %s" % (m, e))
                continue
        for stage in ("vs", "ps"):
            members, dead, dropped, conflicts = bank(entry, masks_numeric, stage, text)
            tot_m += len(members); tot_d += len(dead); tot_c += len(conflicts)
            span = (members[0][0], members[-1][0] + (members[-1][4] or 1)) \
                if members else (0, 0)
            print("%-30s %s членов %3d мёртвых %3d снято %3d диапазон c%d..c%d %s"
                  % (m, stage.upper(), len(members), len(dead), len(dropped),
                     span[0], span[1],
                     "КОНФЛИКТОВ %d" % len(conflicts) if conflicts else ""))
            for a, b in conflicts[:4]:
                print("      c%d %s %s <-> c%d %s %s"
                      % (a[0], a[1], a[2], b[0], b[1], b[2]))
    print("ИТОГО: членов %d, неиспользуемых %d, конфликтов %d"
          % (tot_m, tot_d, tot_c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
