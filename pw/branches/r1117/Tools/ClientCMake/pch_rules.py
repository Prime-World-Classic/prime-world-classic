#!/usr/bin/env python3
"""PCH-правила для CMake-графа клиента (M1/M2, PLAN_client_build_speedup.md §1).

Включается env `PW_PCH` (имена проектов через пробел/запятую, либо `all`).
По умолчанию ВЫКЛЮЧЕНО: граф и артефакт не меняются вовсе.

Конструкция — не `target_precompile_headers` (тот генерирует свой `cmake_pch.h`
и `/FI`, а нам нужен тот же through-header, что уже стоит в PCH-обёртках):

  продюсер   `/Yc<through>` `/Fp<pch>` на TU-обёртке Create-файла проекта.
             Spelling `/Yc` ОБЯЗАН совпадать с текстом `#include` в TU: cl иначе
             даёт `C2857 '#include' statement specified with /Yc... was not found
             in the source` (проверено repro 2026-10-12: относительный
             `#include "stdafx.h"` + `/Yc"Z:/abs/stdafx.h"` = C2857; абсолютный
             spelling в TU = rc 0). Поэтому Create-TU тоже оборачивается.
             Кавычек в флаге быть НЕ должно (см. plan()).
  консьюмер  `/Yu<through> /Fp<тот же .pch>` на уже существующей PCH-обёртке
             (её первая строка и есть `#include "<through>"` от winpath()).
  зависимости `.pch` — побочный файл компиляции продюсера: `OBJECT_OUTPUTS`
             (Ninja/Makefile — поддерживают; «missing => пересобирается»),
             у каждого консьюмера — `OBJECT_DEPENDS`. order-only (`||`) не годится:
             он не делает TU грязным. Актуальность .pch при правке цепочки
             заголовков даёт обычный `deps = msvc` продюсера (он компилируется
             без `/Yu` и видит всю цепочку).
  исключения TU с `UsePrecompiledHeader=0` (не оборачиваются и не получают `/Yu`);
             TU с другим `PrecompiledHeaderThrough` — отдельная пара и свой .pch.

Флаги CRT (`/MT`, `/FS`, `/Fd`) отсюда не трогаются — только `/Yc|/Yu` + `/Fp`.
"""
import hashlib
import os
import re
import sys

PCH_DIR = "pch"


def enabled(name, env=None):
    """PW_PCH: пусто = выключено; all/* = все проекты; иначе список имён."""
    v = (os.environ if env is None else env).get("PW_PCH", "").strip()
    if not v:
        return False
    if v.lower() in ("all", "*", "1", "yes"):
        return True
    return name in re.split(r"[,\s;]+", v)


def pair_tag(th_abs):
    """Стабильное имя пары: базовый заголовок + хэш полного пути (разные
    каталоги с одинаковым stdafx.h не сталкиваются)."""
    base = os.path.splitext(os.path.basename(th_abs))[0]
    key = th_abs.replace("\\", "/").lower()
    return re.sub(r"\W", "_", base) + "__" + hashlib.sha1(key.encode()).hexdigest()[:6]


def pch_cmake_path(proj_name, tag):
    """Путь .pch глазами CMake: раскроется в build-каталог (он и для Wine, и для
    native передан cmake как путь, понятный компилятору: Z:/... или C:/...)."""
    return "${CMAKE_CURRENT_BINARY_DIR}/" + PCH_DIR + "/" + proj_name + "_" + tag + ".pch"


def wrapper_body(th_abs, orig_abs, winpath):
    """Тело обёртки Create-файла — ровно та же форма, что у консьюмеров
    (gen_cmake.py), чтобы spelling /Yc совпал с #include."""
    return ('#include "%s"' + chr(10) + '#line 1 "%s"' + chr(10) + '#include "%s"' + chr(10)) % (
        winpath(th_abs), winpath(orig_abs), winpath(orig_abs))


def plan(proj, winpath):
    """Собирает пары (through-header -> продюсер + консьюмеры) и добавляет
    флаги в per_file. Вызывается в конце Proj.__init__."""
    proj.pch_pairs = {}
    if not enabled(proj.name):
        return
    for th_abs in sorted(proj.pch_cons):
        cons = proj.pch_cons[th_abs]
        tag = pair_tag(th_abs)
        prod = proj.pch_create.get(th_abs)
        if prod is None:
            # нет Create-TU у пары — синтетический продюсер: пустой TU с одним
            # #include (символов не плодит, в линковке безвреден)
            wrel = "%s/%s/_pch_%s.cpp" % (PCH_DIR, proj.name, tag)
            proj.wrappers[wrel] = '#include "%s"' % winpath(th_abs) + chr(10)
            proj.srcs.append(("wrapper", wrel))
            prod_entry = ("wrapper", wrel)
        else:
            rel, absf = prod
            base = os.path.splitext(os.path.basename(absf))[0]
            wrel = "%s/%s/%s_%s.cpp" % (PCH_DIR, proj.name, base, tag)
            proj.wrappers[wrel] = wrapper_body(th_abs, absf, winpath)
            proj.srcs = [("wrapper", wrel) if e == ("src", rel) else e for e in proj.srcs]
            if ("src", rel) in proj.per_file:
                proj.per_file[("wrapper", wrel)] = proj.per_file.pop(("src", rel))
            prod_entry = ("wrapper", wrel)
        pch = pch_cmake_path(proj.name, tag)
        thw = winpath(th_abs)
        # ВАЖНО (спайк 2026-10-12): кавычки в флаг класть НЕЛЬЗЯ. CMake пишет их
        # в build.ninja как /Yc\"path\", а cl видит обратные слебы и не находит
        # #include -> C2857. Без кавычек (/YcZ:/... , /YuZ:/...) — rc 0. Значит
        # путь не должен содержать пробелов; если содержит — пару пропускаем.
        if " " in thw or " " in pch:
            print(f"  !! {proj.name}: PCH пропущен (пробел в пути): {thw}", file=sys.stderr)
            continue
        add = ["/Yc" + thw, "/Fp" + pch]
        o, d = proj.per_file.get(prod_entry, ([], []))
        proj.per_file[prod_entry] = (o + add, d)
        for c in cons:
            oc, dc = proj.per_file.get(c, ([], []))
            proj.per_file[c] = (oc + ["/Yu" + thw, "/Fp" + pch], dc)
        proj.pch_pairs[th_abs] = {"tag": tag, "prod": prod_entry, "cons": list(cons)}
    return proj.pch_pairs


def cmake_lines(proj):
    """Только OBJECT_OUTPUTS/OBJECT_DEPENDS (COMPILE_OPTIONS уже уехали в
    обычный per_file-цикл генератора)."""
    if not getattr(proj, "pch_pairs", None):
        return []
    L = ["file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/" + PCH_DIR + ")"]
    for th_abs in sorted(proj.pch_pairs):
        p = proj.pch_pairs[th_abs]
        pch = pch_cmake_path(proj.name, p["tag"])
        L.append("set_source_files_properties(%s PROPERTIES OBJECT_OUTPUTS \"%s\")" %
                 (proj.cmake_path(p["prod"]), pch))
        for c in p["cons"]:
            L.append("set_source_files_properties(%s PROPERTIES OBJECT_DEPENDS \"%s\")" %
                     (proj.cmake_path(c), pch))
    return L


def summary(proj):
    p = getattr(proj, "pch_pairs", None)
    if not p:
        return ""
    return "PCH %s: %d пар(ы), %d TU" % (
        proj.name, len(p), sum(len(x["cons"]) for x in p.values()) + len(p))
