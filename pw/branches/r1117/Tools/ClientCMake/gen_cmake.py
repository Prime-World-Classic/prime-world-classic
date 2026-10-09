#!/usr/bin/env python3
"""
gen_cmake.py — генерация CMake-проекта клиента по модели parse_vcproj.py.

Назначение (этап 1 плана PLAN_client_modern.md): один и тот же замыканный
исходниками клиент проект (25 проектов, ~1169 .cpp) должен собираться
(a) эталонным VS2008/x86 — чтобы проверить сам ГРАФ (полнота файлов, флаги,
defines, include-пути, линковка) без смены компилятора;
(b) современным MSVC (x64) и (c) GCC/Linux — этапы 2-4.

Почему OBJECT-библиотеки, а не статические .lib: MSVC вытаскивает из архива
только те члены, которые закрывают незакрытый символ. TU, существующие ради
статических регистраций (REGISTER_VAR / REGISTER_DBRESOURCE /
DEFINE_RE_FACTORY / консольные команды), из архива не попадают, и клиент
останавливается после логина («Unknown command or variable "bind"/"login"/
"lobby"» — проверено на tiny10 2026-10-03). $<TARGET_OBJECTS:...> включает в
линовку ВСЕ объекты проекта — это эквивалент того, что делает build_client.py,
линкуя .obj напрямую (см. комментарий у PW_LINK_LIBS).

Семантика флагов снимается с тех же атрибутов vcproj, что и build_client.py
(единственный источник истины — model.json). Отличия от build_client.py
обусловлены CMake/Wine и помечены в коде:
  * PCH не используется: cl под wine не принимает /Yc//Yu с явным
    through-header, а CMake генерирует именно явный (cmake_pch.h). Каждый TU и
    так включает stdafx.h, так что семантика та же, скорость ниже.
  * манифест: mt.exe под wine падает на /outputresource (см. build_client.py),
    поэтому линкуем /MANIFEST:NO и компилируем rc-ресурс 1 24 (RT_MANIFEST)
    с CRT-зависимостью и trustInfo — как wine_app.manifest/wine_manifest.rc в
    драйвере.

Использование:
  python3 parse_vcproj.py <Src> model.json
  python3 gen_cmake.py <Src> model.json <out-dir>
После этого конфигурирование/сборка — см. build_client_cmake.sh.
"""
import json
import os
import re
import sys

CFG = os.environ.get("PW_CFG", "ShippingSingleExe|Win32")
# wine-буква, под которой виден unix-корень (Z: = /). Нужна для #include внутри
# PCH-обёрток (это C++, CMake там не работает).
WINROOT = os.environ.get("PW_WIN_ROOT", "Z:")

# имена проектов из модели (ставится в main) — чтобы исключить .lib самих
# проектов из AdditionalDependencies
PROJ_NAMES = set()

# Флаги cl из AdditionalOptions, которые передаём как есть (список совпадает с
# build_client.py — иначе две сборки собирают разный код).
PASSTHROUGH = {"/Od", "/O1", "/O2", "/Os", "/OX", "/Ob1", "/Ob2", "/Oy-",
               "/MD", "/MDd", "/MT", "/ML", "/EHsc", "/EHa", "/GS-", "/GF",
               "/GL", "/Gy", "/Gm", "/W0", "/W1", "/W2", "/W3", "/W4", "/WX",
               "/RTC1", "/RTCs", "/TP", "/Tc", "/nologo", "/Z7", "/J", "/Gd"}

# wine-специфика VS2008: PDB невозможны (C1902), mt.exe падает.
WINE_LINK_FLAGS = ["/MANIFEST:NO"]


def write_if_changed(path, content):
    """Пишет файл только если содержимое изменилось. gen/ пересоздаётся на каждый
    прогон, и без этого у всех 1074 PCH-обёрток обновляется mtime — ninja
    пересобирает весь граф (~40 мин) даже когда изменились только опции линковки."""
    try:
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
            if fh.read() == content:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(content)
    return True


def q(p):
    """CMake-экранирование пути (в CMakeLists все пути прямые слебы)."""
    return p.replace("\\", "/")


# --- x64: вендорские имена/пути из vcproj описаны для x86 -------------------
# При PW_MACHINE=X64 подменяем только то, что реально есть в дереве (проверено
# по Vendor/): DirectX Lib/x64, DTW lib/amd64, FMOD x64-либы. Пересобранное
# нашим скриптом (Tools/VendorX64/build_vendor_x64.sh) кладётся в <dir>/x64 рядом
# с x86-ной либой: zlib/lib/x64, jpeglib/lib/x64, JsonCpp/lib/Release/x64.
# Остальное (ACE, Terabit, Tamarin, libcurl, OpenSSL, freetype, CrashRpt,
# CxxTest) под x64 ещё не собрано — см. PLAN_client_modern.md, этап 3.
X64_LIB_RENAME = {
    "fmodex_vc": "fmodex64_vc",
    "fmodexl_vc": "fmodexl64_vc",
    "fmod_event": "fmod_event64",
    "fmod_event_net": "fmod_event_net64",
    # Steam: в вендоре есть redistributable_bin/win64/steam_api64.lib
    "steam_api": "steam_api64",
    # GlU32.Lib — x86-ный GLU; под x64 берём Glu32.lib из Windows SDK (он в LIB)
    "glu32": "Glu32",
}
X64_DIR_REMAP = (("Lib/x86", "Lib/x64"), ("lib/i386", "lib/amd64"), ("lib/x86", "lib/x64"),
                 ("redistributable_bin", "redistributable_bin/win64"))
# x86-only вендор, под x64 отсутствует в дереве и не пересобирается:
#   gtrtst32.lib — DirectShow-хелпер (Vendor/DirectShow/Lib): ссылок из Src/ нет;
#   sakijapi.lib — StarForce (Vendor/StarForce): x64-дистрибутива нет, но вызовы
#   PSA_* есть в Src/System/StarForce/StarForce.cpp (строки 73/104/110/117/148) —
#   под x64 эти места надо глушить (_M_X64-guard в StarForce.cpp), иначе LNK2019 на PSA_*.
# Возврат любой либы — PW_DROP_LIBS="".
X64_LIB_DROP_DEFAULT = {"sakijapi", "gtrtst32"}


def x64_lib(name):
    stem, ext = os.path.splitext(os.path.basename(name))
    new = X64_LIB_RENAME.get(stem.lower())
    return name if not new else os.path.join(os.path.dirname(name), new + ext)


def x64_libdir(rel, R1117):
    r = q(rel)
    for a, b in X64_DIR_REMAP:
        if r.endswith(a):
            cand = r[:-len(a)] + b
            if os.path.isdir(os.path.join(R1117, cand)):
                return cand
    # общее правило: пересобранные под x64 либы лежат в <dir>/x64
    cand = r + "/x64"
    if os.path.isdir(os.path.join(R1117, cand)):
        return cand
    return r


def rel_to(base, target):
    """Путь относительно base (для ${PW_SRC}/...), иначе абсолютный."""
    r = os.path.relpath(target, base)
    return q(r) if not r.startswith("..") or os.path.isabs(target) else q(target)


def to_defs(defineds):
    out = []
    for d in (defineds or "").split(";"):
        d = d.strip()
        if d:
            out.append(d)
    return out


def parse_extra_options(opts):
    flags = []
    for tok in (opts or "").split():
        if tok == "/MP":
            continue
        if tok.startswith(("/D", "/F")):
            flags.append(tok)
        elif tok in PASSTHROUGH:
            flags.append(tok)
        else:
            print(f"  !! unhandled AdditionalOption: {tok}", file=sys.stderr)
    return flags


def winpath(p, root=WINROOT):
    """linux path -> wine-путь (Z:\\...). CMake-строки не любят обратные слебы,
    но они внутри кавычек C++ #include — там это экранирование C, поэтому
    слебы прямые (как в PCH-обёртках build_client.py)."""
    return root + q(os.path.normpath(p))


def resolve_through(proj_dir, src_abs, th, includes):
    """Через какой заголовок строится PCH. Копия логики build_client.py:
    каталог проекта важнее заголовка с тем же именем в каталоге файла
    (Game/PF/Server/Statistic/stdafx.h — заглушка)."""
    cands = [proj_dir, os.path.dirname(src_abs)] + includes
    t = th.replace("\\", "/")
    for base in cands:
        p = os.path.normpath(os.path.join(base, t))
        if os.path.isfile(p):
            return p
        d, low_name = os.path.split(p)[0], os.path.basename(t).lower()
        try:
            entries = os.listdir(d)
        except OSError:
            continue
        match = [e for e in entries if e.lower() == low_name]
        if len(match) == 1:
            return os.path.join(d, match[0])
    return None


def case_resolve(base, rel):
    """Resolve rel (Windows-разделители, возможен неверный регистр) против
    реального регистра ФС. Копия из build_client.py — обе сборки должны
    разрешать пути одинаково."""
    parts = [p for p in re.split(r"[\\/]+", rel.replace("\\", "/")) if p not in ("", ".")]
    cur = base
    for part in parts:
        if part == "..":
            cur = os.path.dirname(cur)
            continue
        try:
            entries = os.listdir(cur)
        except OSError:
            return None
        if part in entries:
            cur = os.path.join(cur, part)
            continue
        low = part.lower()
        match = [e for e in entries if e.lower() == low]
        if len(match) == 1:
            cur = os.path.join(cur, match[0])
        else:
            return None
    return cur


class Proj:
    def __init__(self, name, m, SRC, R1117):
        self.name = name
        self.m = m
        self.dir = m["dir"]
        self.is_exe = m["type"] == "1"
        cl = m["tools"].get("VCCLCompilerTool", {})
        self.opts = []
        opt = cl.get("Optimization", "0")
        self.opts.append({0: "/Od", 1: "/O1", 2: "/O2", 3: "/Os", 4: "/OX"}.get(opt, "/O2"))
        if cl.get("InlineFunctionExpansion") in ("1", "2"):
            self.opts.append("/Ob" + cl["InlineFunctionExpansion"])
        self.opts.append({0: "/ML", 1: "/MT", 2: "/MD", 3: "/MDd"}.get(cl.get("RuntimeLibrary", "2"), "/MD"))
        eh = cl.get("ExceptionHandling", "0")
        if eh == "1":
            self.opts.append("/EHsc")
        elif eh == "2":
            self.opts.append("/EHa")
        if cl.get("BufferSecurityCheck") == "false":
            self.opts.append("/GS-")
        if cl.get("EnableFunctionLevelLinking") == "true":
            self.opts.append("/GF")
        self.opts.append("/W" + cl.get("WarningLevel", "3"))
        if cl.get("WarnAsError") == "true":
            self.opts.append("/WX")
        self.opts += parse_extra_options(cl.get("AdditionalOptions", ""))
        # C4519 — консервативное предупреждение MSVC 9 (легальный C++), при /WX
        # ломает RpcArgs.h, добавленный Linux-портом. Совпадает с драйвером.
        self.opts.append("/wd4519")
        # доп. флаги cl из окружения (аналог PW_EXTRA_OPTS в build_client.py):
        # этапы 2-3 (современный компилятор) — /std:c++17, /wdNNNN, /WX-.
        # Добавляются ПОСЛЕ vcproj-флагов: cl берёт последнее вхождение (/WX-
        # перебивает /WX из vcproj, /std: перебивает дефолт).
        self.opts += os.environ.get("PW_EXTRA_OPTS", "").split()

        self.defs = to_defs(cl.get("PreprocessorDefinitions", ""))
        self.defs += to_defs(os.environ.get("PW_EXTRA_DEFS", ""))

        self.includes = []
        for inc in (cl.get("AdditionalIncludeDirectories", "") or "").split(";"):
            inc = inc.strip()
            if not inc:
                continue
            real = case_resolve(self.dir, inc) or case_resolve(SRC, inc)
            if real is None:
                print(f"  !! {name}: unresolved include dir {inc!r}", file=sys.stderr)
                continue
            self.includes.append(rel_to(SRC, real))

        rc = m["tools"].get("VCResourceCompilerTool", {})
        self.rc_defs = to_defs(rc.get("PreprocessorDefinitions", ""))
        # CMake для своих rc-правил сам ставит -D; в add_custom_command команда
        # голубая, поэтому префиксы на месте
        self.rc_defs_prefixed = ["/D" + d for d in self.rc_defs]
        self.rc_incs_prefixed = []
        self.rc_includes = []
        for inc in (rc.get("AdditionalIncludeDirectories", "") or "").split(";"):
            inc = inc.strip()
            if not inc:
                continue
            real = case_resolve(self.dir, inc)
            if real:
                r = rel_to(SRC, real)
                if r not in self.includes:
                    # rc-пути добавляем к include-путям цели: CMake сам отдаёт их
                    # как -I и для rc, отдельный /i не нужен
                    self.includes.append(r)
                if r not in self.rc_includes:
                    self.rc_includes.append(r)

        self.srcs, self.res, self.per_file, self.res_out = [], [], {}, []
        self.pch_through = (cl.get("PrecompiledHeaderThrough") or "stdafx.h").strip()
        self.pch_on = cl.get("UsePrecompiledHeader", "0")
        self.wrappers = {}
        for f in m["files"]:
            absf, low = f["abs"], f["rel"].lower()
            if not os.path.isfile(absf):
                continue
            per = f["cfg"].get("VCCLCompilerTool", {})
            if per.get("ExcludedFromBuild") == "true":
                continue
            if low.endswith(".rc"):
                self.res.append(rel_to(SRC, absf))
                continue
            if not low.endswith((".cpp", ".c")):
                continue
            rel = rel_to(SRC, absf)
            # PCH: cl под wine не принимает /Yc//Yu с явным through-header, а
            # CMake умеет только явный. Поэтому — обёртка, как в build_client.py:
            # она включает through-header проекта, затем оригинал. Без обёртки
            # #include <stdafx.h> из угла поиска цепляет ЧУЖОЙ stdafx.h
            # (в include-путях есть Src/Server и Src/Game/PF) -> в TU нет
            # контекста проекта, DBStats.h: "ExecutableFloatString undeclared".
            up = per.get("UsePrecompiledHeader", "")
            if not low.endswith(".c") and self.pch_on == "2" and up not in ("0", "1"):
                th = (per.get("PrecompiledHeaderThrough") or self.pch_through or "stdafx.h").strip()
                th_abs = resolve_through(self.dir, absf, th,
                                         [os.path.normpath(os.path.join(SRC, i)) for i in self.includes])
                if th_abs:
                    base = os.path.splitext(os.path.basename(absf))[0]
                    wrel = f"pch/{self.name}/{base}.cpp"
                    self.wrappers[wrel] = ('#include "%s"' + chr(10) + '#line 1 "%s"' + chr(10) +
                                          '#include "%s"' + chr(10)) % (
                                          winpath(th_abs), winpath(absf), winpath(absf))
                    self.srcs.append(("wrapper", wrel))
                else:
                    print(f"  !! {self.name}: cannot resolve through header {th!r} for {f['rel']}",
                          file=sys.stderr)
                    self.srcs.append(("src", rel))
            else:
                self.srcs.append(("src", rel))
            # пофайловые переопределения из vcproj (WarningLevel,
            # ExceptionHandling, свои /D, AdditionalOptions)
            o, d = [], []
            if per.get("WarningLevel"):
                o.append("/W" + per["WarningLevel"])
            peh = per.get("ExceptionHandling")
            if peh == "1":
                o.append("/EHsc")
            elif peh == "2":
                o.append("/EHa")
            o += parse_extra_options(per.get("AdditionalOptions", ""))
            d += to_defs(per.get("PreprocessorDefinitions", ""))
            if o or d:
                key = ("wrapper", wrel) if (self.srcs and self.srcs[-1][0] == "wrapper"
                                            and self.srcs[-1][1] == wrel) else ("src", rel)
                self.per_file[key] = (o, d)

        self.rc_incs_prefixed = ["/i${PW_SRC}/" + x for x in self.rc_includes]

        link = m["tools"].get("VCLinkerTool", {})
        # AdditionalDependencies содержит и вендорные .lib, и .lib самих проектов
        # (в VS они линкуются как архивы). В нашей сборке объекты проектов уже
        # входят в линковку через $<TARGET_OBJECTS:...> — архивы проектов
        # исключены, иначе линкер тянул бы из них лишние члены.
        self.link_libs = [x.strip() for x in (link.get("AdditionalDependencies", "") or "").split()
                          if x.strip() and os.path.splitext(os.path.basename(x.strip()))[0] not in PROJ_NAMES]
        # PW_DROP_LIBS — библиотеки, которые тулчейн не даёт (или давать не должен).
        # Например comsuppw.lib удалён из VC начиная с VS2017 15.3: _com_error /
        # _com_ptr_t переехали в заголовки (<comdef.h>), линковать их нечем и не нужно.
        drop = {x.strip().lower() for x in os.environ.get("PW_DROP_LIBS", "").split()}
        if drop:
            self.link_libs = [x for x in self.link_libs
                              if os.path.splitext(os.path.basename(x))[0].lower() not in drop]
        self.link_dirs = []
        for d in (link.get("AdditionalLibraryDirectories", "") or "").split(";"):
            d = d.strip().strip('"').strip()
            if not d or d == "$(OutDir)":
                continue
            real = case_resolve(self.dir, d) or case_resolve(R1117, d)
            if real is None:
                print(f"  !! {name}: unresolved libdir {d!r}", file=sys.stderr)
                continue
            self.link_dirs.append(rel_to(R1117, real))
        self.nodefault = [x for x in (link.get("IgnoreDefaultLibraryNames", "") or "").split(";") if x]
        # x64: подмена x86-ных вендорских имён и каталогов (см. X64_LIB_RENAME)
        if os.environ.get("PW_MACHINE", "X86") == "X64":
            drop = {x.strip().lower() for x in os.environ.get("PW_DROP_LIBS", "").split()}
            drop |= X64_LIB_DROP_DEFAULT
            self.link_libs = [x64_lib(x) for x in self.link_libs
                              if os.path.splitext(os.path.basename(x))[0].lower() not in drop]
            self.link_dirs = [x64_libdir(d, R1117) for d in self.link_dirs]
        # PW_DROP_NODEFAULT — убрать из vcproj-списка «не линковать эти CRT»:
        # x64-сборка идёт на /MT (статический CRT), а vcproj запрещает libcmt.lib,
        # потому что сам он /MD.
        drop_nd = {x.strip().lower() for x in os.environ.get("PW_DROP_NODEFAULT", "").split()}
        if drop_nd:
            self.nodefault = [x for x in self.nodefault
                              if x.strip().lower() not in drop_nd]
        self.subsys = {1: "CONSOLE", 2: "WINDOWS"}.get(int(link.get("SubSystem", "2")), "WINDOWS")
        self.laa = str(link.get("LargeAddressAware", "")).strip()

    @staticmethod
    def cmake_path(entry):
        kind, rel = entry
        # GENDIR задаётся в верхнем CMakeLists: внутри projects/<X>.cmake
        # CMAKE_CURRENT_LIST_DIR = каталог projects/, а не gen/
        return ("${PW_SRC}/" + rel) if kind == "src" else ("${GENDIR}/" + rel)

    def cmake(self, SRC, R1117):
        n = self.name
        L = []
        if self.is_exe:
            # exe-проект: целей add_library нет, его источники перечислены в
            # add_executable; сюда остаются только пофайловые свойства
            srcs = None
        else:
            srcs = " ".join(self.cmake_path(x) for x in self.srcs)
            L.append(f"add_library({n} OBJECT {srcs})")
        if not self.is_exe and self.includes:
            L.append(f"target_include_directories({n} PRIVATE " +
                     " ".join(f"${{PW_SRC}}/{i}" for i in self.includes) + ")")
        if not self.is_exe and self.defs:
            L.append(f"target_compile_definitions({n} PRIVATE " +
                     " ".join(self.defs) + ")")
        if not self.is_exe:
            L.append(f"target_compile_options({n} PRIVATE " + " ".join(self.opts) + ")")
        # .rc НЕ входят в OBJECT-цель: rc резолвит ICON/BITMAP/CURSOR-пути
        # относительно CWD (в VS это каталог проекта; у Ninja CWD = build-каталог,
        # и rc даёт RC2135 "file not found: error.ico"). Поэтому для каждого .rc
        # — add_custom_command с WORKING_DIRECTORY = каталог .rc (как в
        # build_client.py), а результат (.res) кладётся в линковку exe.
        # Форма /fo<file> без двоеточия: под wine "/fo:x.res" создаёт файл с
        # именем ":x.res".
        for r in self.res:
            base = re.sub(r"\W", "_", n) + "_" + os.path.splitext(os.path.basename(r))[0]
            out_res = "${CMAKE_CURRENT_BINARY_DIR}/" + base + ".res"
            cmd = ["rc", "/fo" + out_res] + self.rc_defs_prefixed + self.rc_incs_prefixed + [
                "${PW_SRC}/" + r]
            L.append("add_custom_command(OUTPUT " + out_res)
            L.append("  COMMAND " + " ".join(cmd))
            L.append(f"  DEPENDS ${{PW_SRC}}/{r}")
            # CWD = каталог ПРОЕКТА (как в build_client.py), а не каталог .rc:
            # Foundation/Profiler3UI.rc лежит в System/InlineProfiler3, а
            # error.ico/exclamation.ico — в System/
            L.append(f"  WORKING_DIRECTORY ${{PW_SRC}}/{rel_to(SRC, self.dir)}" + ")")
            self.res_out.append(base + ".res")
        for entry, (o, d) in self.per_file.items():
            parts = []
            if o:
                parts.append(f"COMPILE_OPTIONS \"{' '.join(o)}\"")
            if d:
                parts.append(f"COMPILE_DEFINITIONS \"{' '.join(d)}\"")
            L.append(f"set_source_files_properties({self.cmake_path(entry)} PROPERTIES " +
                     " ".join(parts) + ")")
        return "\n".join(L) + "\n"


def main():
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    SRC = os.path.abspath(sys.argv[1])
    model = json.load(open(sys.argv[2]))
    out = os.path.abspath(sys.argv[3])
    R1117 = os.path.dirname(SRC)
    os.makedirs(os.path.join(out, "projects"), exist_ok=True)

    names = [n for n in model if n != "PW_Game"]
    global PROJ_NAMES
    PROJ_NAMES = set(model)
    projs = {n: Proj(n, model[n], SRC, R1117) for n in model}

    # порядок не важен для OBJECT-библиотек (линковка одна), но детерминизм полезен
    for n in sorted(model):
        write_if_changed(os.path.join(out, "projects", n + ".cmake"),
                         projs[n].cmake(SRC, R1117))

    # --- манифест (аналог wine_app.manifest/wine_manifest.rc в драйвере) ----
    app_manifest = os.path.join(SRC, "Application.manifest")
    # CRT-зависимость манифеста — по компилятору/мишени: VS2008/x86 — VC90.CRT,
    # VS2022 — VC143.CRT (для x64-сборок этапа 3)
    if os.environ.get("PW_MACHINE", "X86") == "X64":
        crt_and_priv = (
            '<dependency><dependentAssembly><assemblyIdentity type="win32" '
            'name="Microsoft.VC143.CRT" version="14.44.35207" '
            'processorArchitecture="x64" publicKeyToken="1fc8b3b9a1e18e3b"/>'
            '</dependentAssembly></dependency>'
            '<trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security>'
            '<requestedPrivileges><requestedExecutionLevel level="asInvoker" '
            'uiAccess="false"/></requestedPrivileges></security></trustInfo>\n')
    else:
        crt_and_priv = (
            '<dependency><dependentAssembly><assemblyIdentity type="win32" '
            'name="Microsoft.VC90.CRT" version="9.0.21022.8" '
            'processorArchitecture="x86" publicKeyToken="1fc8b3b9a1e18e3b"/>'
            '</dependentAssembly></dependency>'
            '<trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security>'
            '<requestedPrivileges><requestedExecutionLevel level="asInvoker" '
            'uiAccess="false"/></requestedPrivileges></security></trustInfo>\n')
    manifest_cmd = None
    manifest_res = None
    if os.path.isfile(app_manifest):
        text = open(app_manifest, encoding="utf-8-sig").read()
        merged = text.replace("</assembly>", crt_and_priv + "</assembly>")
        if "Microsoft.VC90.CRT" not in merged:
            merged = text
        write_if_changed(os.path.join(out, "wine_app.manifest"), merged)
        write_if_changed(os.path.join(out, "wine_manifest.rc"), '1 24 "wine_app.manifest"\n')
        # тоже custom command: rc ищет wine_app.manifest относительно CWD
        manifest_cmd = ('add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wine_manifest.res\n'
                        '  COMMAND rc /fo${CMAKE_CURRENT_BINARY_DIR}/wine_manifest.res wine_manifest.rc\n'
                        '  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/wine_manifest.rc\n'
                        '  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})')
        manifest_res = "${CMAKE_CURRENT_BINARY_DIR}/wine_manifest.res"

    pwg = projs["PW_Game"]
    objs = " ".join(f"$<TARGET_OBJECTS:{n}>" for n in sorted(names))
    top = []
    top.append("cmake_minimum_required(VERSION 3.15)")
    # C включён: в замыкании есть .c (Scripts/lua/*.c, Foundation/Math/ieehalfprecision.c);
    # без языка C CMake их молча не компилирует -> LNK2019 на lua_*/halfp2singles.
    top.append("project(PWClient C CXX RC)")
    top.append("# флаги полностью из vcproj (см. gen_cmake.py); дефолты CMake для MSVC")
    top.append("# (/DWIN32 /D_WINDOWS /GR /EHsc, /Zi в Debug) нам не нужны и ломают wine-сборку")
    top.append("set(CMAKE_CXX_FLAGS \"\")")
    top.append("set(CMAKE_C_FLAGS \"\")")
    top.append("set(GENDIR ${CMAKE_CURRENT_LIST_DIR})")
    top.append("foreach(p " + " ".join(sorted(model)) + ")")
    top.append("  include(${CMAKE_CURRENT_LIST_DIR}/projects/${p}.cmake)")
    top.append("endforeach()")
    if manifest_cmd:
        top.append(manifest_cmd)
    res_all = [f"${{CMAKE_CURRENT_BINARY_DIR}}/{r}" for n in sorted(model)
               for r in projs[n].res_out]
    # pwg.res сюда НЕ добавляются: каждый .rc уже представлен своим
    # add_custom_command (.res в res_all)
    exe_srcs = " ".join(Proj.cmake_path(x) for x in pwg.srcs)
    if manifest_res:
        exe_srcs += " " + manifest_res
    exe_srcs += " " + " ".join(res_all)
    # WIN32 -> /subsystem:windows + точка входа WinMain; .res помечаются
    # EXTERNAL_OBJECT, иначе CMake их в линковку не кладёт (манифест 1/24
    # отсутствует -> нет зависимости VC90.CRT, на клиентской ВМ процесс стартует
    # и сразу выходит)
    top.append(f"add_executable(PW_Game WIN32 {exe_srcs} {objs})")
    # ВСЕ .res (в т.ч. wine_manifest.res) обязаны быть помечены
    # EXTERNAL_OBJECT/GENERATED: без этого CMake не кладёт файл в линковку —
    # молча, без ошибки. Проверено: манифест 1/24 генерировался (rc отрабатывал,
    # wine_manifest.res 1116 Б), но в build.ninja в `build PW_Game.exe:` его не
    # было → в exe нет ни Microsoft.VC90.CRT, ни asInvoker → на Windows процесс
    # падает сразу (0xC0000135, лог-папка не создаётся).
    ext_obj = list(res_all)
    if manifest_res:
        ext_obj.append(manifest_res)
    if ext_obj:
        top.append("set_source_files_properties(" + " ".join(ext_obj) +
                   " PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)")
    if pwg.includes:
        top.append("target_include_directories(PW_Game PRIVATE " +
                   " ".join(f"${{PW_SRC}}/{i}" for i in pwg.includes) + ")")
    if pwg.defs:
        top.append("target_compile_definitions(PW_Game PRIVATE " + " ".join(pwg.defs) + ")")
    top.append("target_compile_options(PW_Game PRIVATE " + " ".join(pwg.opts) + ")")
    if pwg.link_dirs:
        top.append("target_link_directories(PW_Game PRIVATE " +
                   " ".join(f"${{R1117}}/{d}" for d in pwg.link_dirs) + ")")
    if pwg.link_libs:
        top.append("target_link_libraries(PW_Game PRIVATE " + " ".join(pwg.link_libs) + ")")
    lf = ["/MACHINE:" + os.environ.get("PW_MACHINE", "X86")] + WINE_LINK_FLAGS
    if pwg.laa == "2":
        lf.append("/LARGEADDRESSAWARE")     # бит LAA: 4 ГБ вместо 2 ГБ (см. PLAN_client_oom.md)
    elif pwg.laa == "1":
        lf.append("/LARGEADDRESSAWARE:NO")
    lf += ["/NODEFAULTLIB:" + x for x in pwg.nodefault]
    top.append("target_link_options(PW_Game PRIVATE " + " ".join(lf) + ")")
    top.append("set_target_properties(PW_Game PROPERTIES OUTPUT_NAME PW_Game)")

    write_if_changed(
        os.path.join(out, "CMakeLists.txt"),
        "# СГЕНЕРИРОВАННО из model.json (Tools/ClientCMake/gen_cmake.py) — не править руками\n"
        + "\n".join(top) + "\n")

    for n in model:
        for wrel, content in projs[n].wrappers.items():
            write_if_changed(os.path.join(out, wrel), content)

    n_src = sum(len(projs[n].srcs) for n in model)
    n_wrap = sum(len(projs[n].wrappers) for n in model)
    n_res = sum(len(projs[n].res) for n in model)
    print(f"projects={len(model)} cpp/c={n_src} (pch-wrappers={n_wrap}) rc={n_res} -> {out}")


if __name__ == "__main__":
    main()
