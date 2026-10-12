#!/usr/bin/env bash
# =============================================================================
# build_client_x64_wine.sh — сборка ShippingSingleExe x64 (PW_Game.exe, cl 14.44,
# /MT) под Wine. Нативный Windows-эквивалент — build_client_x64_win.cmd: он
# вызывает ТОЧНО ЭТУ генерацию (тот же gen_cmake.py, те же опции) без Wine, см.
# BUILD_CLIENT_X64.md.
#
# Граф строится из model.json (parse_vcproj.py) генератором
# Tools/ClientCMake/gen_cmake.py; проекты — OBJECT-библиотеки, в линковку
# попадают ВСЕ объекты (статические .lib теряют TU ради статических регистраций:
# «Unknown command or variable "bind"/"login"/"lobby"», зависание после
# «Got login reply»).
#
# Использование:
#   ./build_client_x64_wine.sh                 # конфигурирование + сборка
#   ./build_client_x64_wine.sh --clean         # удалить gen- и build-каталоги
#   ./build_client_x64_wine.sh --gen-only      # только сгенерировать CMakeLists
#   ./build_client_x64_wine.sh --prefix=PATH   # другой Wine-префикс
#   ./build_client_x64_wine.sh --keep-going    # ninja -k0 (обзор ошибок компиляции)
#   ./build_client_x64_wine.sh --check         # только сверка артефакта
#   PW_BUILD_JOBS=8 ./build_client_x64_wine.sh
#   PW_EXTRA_OPTS / PW_EXTRA_DEFS / PW_EXTRA_LINK_OPTS — см. ниже
#
# Хуки плана PLAN_client_build_speedup.md (PCH + кэш объектов):
#   PW_PCH=<список проектов|all>  — уходит в gen_cmake.py через окружение
#   PW_NINJA_STATS=1              — ninja -d stats в лог
#   PW_WINE_SYNC=fsync|esync      — WINEFSYNC/WINEESYNC (замер по плану, §3)
#   Кэш объектов (PW_CACHE_LAUNCHER) в этом скрипте НЕ работает: ninja.exe —
#   windows-бинарь, он не может запустить linux-лаунчер (план, §2 маршруты A/B);
#   для кэша — нативный путь build_client_x64_win.cmd.
#
# Мишень жёстко X64 (PW_MACHINE=X64), конфигурация ShippingSingleExe|Win32
# (Win32 — имя конфигурации в vcproj, разрядность задаёт /MACHINE:X64).
#
# Требования: Wine-префикс с VS2022 (cl 14.44, x64) + Windows Kits 10
#   (по умолчанию ~/.wine-vs) + Windows cmake.exe и ninja.exe:
#     ~/pwbuild/wintools/cmake-3.31.6-windows-x86_64/bin/cmake.exe
#     ~/pwbuild/wintools/ninja.exe
#   (PW_CMAKE_DIR / PW_NINJA переопределяют).
#
# Опции по умолчанию для x64 (почему — см. BUILD_CLIENT_X64.md и скил pw-client):
#   /MT            — в тулчейне нет msvcprt.lib, зато есть весь статический CRT
#                    x64; ВЕСЬ x64-вендор собран /MT, смешивать с /MD нельзя.
#   /WX-           — /WX из vcproj с cl 14.44 даёт ~4,5 тыс. ошибок на новых
#                    предупреждениях (C4458, C4244/C4267 = сигналы 64-битных
#                    сужений, их разбирать списком, а не молча гасить).
#   PW_DROP_LIBS   — comsuppw/comsuppwd (удалены из VC с VS2017 15.3).
#   PW_DROP_NODEFAULT=libcmt.lib — vcproj запрещает libcmt (он же /MD), для /MT
#                    он нужен.
#   Дополнительно снимает/подменяет сам gen_cmake.py (X64_*): sakijapi (StarForce,
#   x64-дистрибутива нет, вызовы PSA_* закрыты отсутствием STARFORCE_PROTECTED),
#   gtrtst32, cxxtest (мертва), crashrpt (требует VC.ATL, код закрыт
#   #if !defined(_M_X64)); fmod*→fmod*64, steam_api→steam_api64, Lib/x86→Lib/x64.
#
# Проверка конфигурации Wine (все три — обход ограничений Wine):
#   -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT= : cl под wine не пишет PDB
#       (fatal C1902 "Program database manager mismatch"); CMake по умолчанию
#       добавляет /Zi + /Fd.
#   -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO  : mt.exe /outputresource под wine
#       падает (access violation). Манифест вместо этого — rc-ресурс 1 24,
#       который генерирует gen_cmake.py.
#   -DCMAKE_TRY_COMPILE_CONFIGURATION=Release: try-compile иначе идёт с Debug-
#       флагами (/Zi) и падает на том же C1902.
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
BR="$REPO/pw/branches/r1117"
WCB="$BR/Tools/WineClientBuild"
CCT="$BR/Tools/ClientCMake"
CFG="${PW_CFG:-ShippingSingleExe|Win32}"
TOOLCHAIN="vs2022"
# тег gen/build-каталогов и cmd-обёртки. PW_BUILD_TAG позволяет держать НЕСКОЛЬКО
# графов одновременно (x64-wine — база, x64-wine-pch — с PCH): сравнение по
# build_time.py и инкрементальные прогоны не затирают базовый каталог.
TC_TAG="${PW_BUILD_TAG:-x64-wine}"
WINEPREFIX="${WINEPREFIX:-}"
JOBS="${PW_BUILD_JOBS:-8}"
CMAKE_DIR="${PW_CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"

DO_CLEAN=0; DO_GEN_ONLY=0; DO_KEEP=0; DO_CHECK=0; PREFIX_FORCED=0
for arg in "$@"; do
    case "$arg" in
        --prefix=*)   WINEPREFIX="${arg#--prefix=}"; PREFIX_FORCED=1 ;;
        --clean)      DO_CLEAN=1 ;;
        --gen-only)   DO_GEN_ONLY=1 ;;
        --keep-going) DO_KEEP=1 ;;
        --check)      DO_CHECK=1 ;;
        -h|--help)    grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -60; exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done

# build-каталог и gen-каталог — ОТДЕЛЬНЫЕ от build_client_cmake.sh: кэш cmake
# привязан и к компилятору, и к каталогу исходников (gen/), поэтому два
# прогона в одном build-каталоге вынуждают пересобирать весь граф (1145 TU).
# Набор флагов при этом обязан быть идентичным — см. BUILD_CLIENT_X64.md.
BUILD="$CCT/build/$TC_TAG"
GEN="$CCT/gen/$TC_TAG"
KEEP=""
if [ "$DO_KEEP" = 1 ]; then KEEP="-k0"; fi

# ------------------------------------------------------------- 0. Wine-префикс
VS22_UNIX='drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools'
KITS_UNIX='drive_c/Program Files (x86)/Windows Kits/10'
prefix_ok() {
    local p="$1"
    [ -d "$p" ] || return 1
    ls "$p/$VS22_UNIX/VC/Tools/MSVC/"*/bin/HostX64/x64/cl.exe >/dev/null 2>&1 || return 1
    ls -d "$p/$KITS_UNIX/Include/"10.* >/dev/null 2>&1 || return 1
    return 0
}
if [ "$PREFIX_FORCED" != 1 ]; then
    if [ -z "$WINEPREFIX" ]; then WINEPREFIX="$HOME/.wine-vs"; fi
    if ! prefix_ok "$WINEPREFIX"; then
        for cand in "$HOME/.wine-vs" "$HOME/pwbuild/wine32" "$HOME/.wine" \
                    $(ls -d "$HOME"/*/wine* "$HOME"/.wine* 2>/dev/null); do
            if prefix_ok "$cand"; then WINEPREFIX="$cand"; break; fi
        done
    fi
fi
prefix_ok "$WINEPREFIX" || {
    echo "ERROR: в префиксе '$WINEPREFIX' нет VS2022 (x64) + Windows Kits 10." >&2
    echo "Нужно: VC\\Tools\\MSVC\\<ver>\\bin\\HostX64\\x64\\cl.exe и Include\\10.*." >&2
    exit 1
}
echo "== Wine-префикс: $WINEPREFIX (тулчейн $TOOLCHAIN, x64)"
export WINEPREFIX
[ -x "$CMAKE_DIR/bin/cmake.exe" ] || { echo "ERROR: cmake.exe не найден: $CMAKE_DIR" >&2; exit 1; }
[ -f "$NINJA" ] || { echo "ERROR: ninja.exe не найден: $NINJA" >&2; exit 1; }

# ------------------------------------------------- 0b. server_ip.h (per-deploy)
IPH="$BR/Src/PW_Game/server_ip.h"
if [ -f "$WCB/server_ip.env" ]; then set -a; . "$WCB/server_ip.env"; set +a; fi
: "${PW_SERVER_IP:=127.0.0.1}"
: "${PW_SESSION_TOKEN:=Tester00Tester00Tester00Tester00}"
: "${PW_API_KEY:=changeme}"
: "${PW_BACKEND_HTTP_PORT:=7777}"
: "${PW_SERVER_PORT:=27300}"
: "${PW_LOGIN_PORT:=27301}"
: "${PW_CLUSTER_PORT_FRONT:=27310}"
: "${PW_CLUSTER_PORT_BACK:=27340}"
: "${PW_SYNCHRONIZER_PORT:=27302}"
if [ ! -f "$IPH" ]; then
    echo "== генерация Src/PW_Game/server_ip.h (SERVER_IP=$PW_SERVER_IP)"
    sed -e "s|@SERVER_IP@|$PW_SERVER_IP|g" -e "s|@SESSION_TOKEN@|$PW_SESSION_TOKEN|g" \
        -e "s|@API_KEY@|$PW_API_KEY|g" -e "s|@BACKEND_HTTP_PORT@|$PW_BACKEND_HTTP_PORT|g" \
        -e "s|@SERVER_PORT@|$PW_SERVER_PORT|g" -e "s|@LOGIN_PORT@|$PW_LOGIN_PORT|g" \
        -e "s|@CLUSTER_PORT_FRONT@|$PW_CLUSTER_PORT_FRONT|g" \
        -e "s|@CLUSTER_PORT_BACK@|$PW_CLUSTER_PORT_BACK|g" \
        -e "s|@SYNCHRONIZER_PORT@|$PW_SYNCHRONIZER_PORT|g" \
        "$WCB/server_ip.h.template" > "$IPH"
else
    echo "== server_ip.h уже есть — не перезаписываю"
fi

# ------------------------------------------- 0c. мишень и опции (ДО генерации!)
# PW_MACHINE читает gen_cmake.py (/MACHINE, CRT-зависимость манифеста, x64-подмены
# вендора), поэтому блок обязан стоять ДО шага 1.
export PW_MACHINE=X64
: "${PW_DEFAULT_OPTS:=/WX- /MT}"
: "${PW_DROP_LIBS:=comsuppw comsuppwd}"
: "${PW_DROP_NODEFAULT:=libcmt.lib}"
# PW_EXTRA_OPTS ДОПОЛНЯЕТ дефолты тулчейна, а не заменяет их (иначе молча
# снимаются /WX- и /MT, и сборка падает по причинам, к правке не относящимся).
PW_EXTRA_OPTS="${PW_DEFAULT_OPTS} ${PW_EXTRA_OPTS:-}"
export PW_EXTRA_OPTS PW_DROP_LIBS PW_DROP_NODEFAULT
# замеры по плану §3: режим синхронизации Wine и статистика ninja включаются
# только явно — по умолчанию поведение не меняется
if [ -n "${PW_WINE_SYNC:-}" ]; then
    case "$PW_WINE_SYNC" in
        fsync) export WINEFSYNC=1 ;;
        esync) export WINEESYNC=1 ;;
        *) echo "ERROR: PW_WINE_SYNC=fsync|esync" >&2; exit 2 ;;
    esac
    echo "== Wine sync: $PW_WINE_SYNC"
fi
NSTATS=""
if [ "${PW_NINJA_STATS:-}" = 1 ]; then NSTATS="-d stats"; fi
# Маршрут A кэша объектов (план §2): лаунчер под Wine обязан быть WINDOWS-экзешником
# (wine-ninja не запускает linux-бинарь — поэтому pwcache только в нативном пути,
# а здесь — sccache.exe). Значение — путь к sccache.exe или имя в PATH.
LAUNCHER=""
if [ -n "${PW_CACHE_LAUNCHER:-}" ]; then
    LAUNCHER="$PW_CACHE_LAUNCHER"      # unix-путь; Z:-префикс добавит python-блок
    echo "== кэш объектов: launcher $PW_CACHE_LAUNCHER (маршрут A, план §2)"
fi
echo "== мишень: /MACHINE:X64, конфигурация: $CFG"
echo "== доп. флаги cl: ${PW_EXTRA_OPTS}"
echo "== снимаемые из линковки .lib: $PW_DROP_LIBS (+ x64-подмены генератора)"

# компиляторное окружение префикса: cl/link из bin\HostX64\x64, либы lib\x64 + Kits
MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
CL_PATH="$MSVC_WIN\\$MSVCVER\bin\HostX64\x64;$KITS_WIN\bin\x64"
CL_INC="$MSVC_WIN\\$MSVCVER\include;$KITS_WIN\Include\\${KVER}\ucrt;$KITS_WIN\Include\\${KVER}\um;$KITS_WIN\Include\\${KVER}\shared"
CL_LIB="$MSVC_WIN\\$MSVCVER\lib\x64;$KITS_WIN\Lib\\${KVER}\ucrt\x64;$KITS_WIN\Lib\\${KVER}\um\x64"
echo "== MSVC $MSVCVER, Windows Kits $KVER"

# ------------------------------------------------------- 1. модель + генерация
echo "== 1/3. model.json + генерация CMake"
python3 "$WCB/parse_vcproj.py" "$BR/Src" "$CCT/model.json" "$CFG" >/dev/null
[ "$DO_CLEAN" = 1 ] && rm -rf "$BUILD" "$GEN"
# gen/ НЕ пересоздаётся каждый прогон: gen_cmake.py пишет файлы только при
# изменении содержимого, иначе ninja пересобирает 1145 PCH-обёрток из-за свежего
# mtime (полный прогон ~40 мин против перелинковки за минуты).
mkdir -p "$BUILD"
python3 "$CCT/gen_cmake.py" "$BR/Src" "$CCT/model.json" "$GEN"
# cmd-обёртка пишется и при --gen-only: её содержимое (пути, окружение тулчейна,
# аргументы cmake) — часть проверки того, что конфигурирование было бы корректным.
cat > "$CCT/$TC_TAG.cmd" <<'CMDEOF'
@echo off
set PATH=__CMAKE_WIN__;__NINJADIR_WIN__;__CL_PATH__;%PATH%
set INCLUDE=__CL_INC__
set LIB=__CL_LIB__
set WINEDEBUG=-all
cd /d __BUILD_WIN__
cmake -G Ninja -DPW_SRC=__SRC_FWD__ -DR1117=__R1117_FWD__ ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" ^
  -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO ^
  -DCMAKE_MAKE_PROGRAM=__NINJA_FWD__ __LAUNCHER__ ^
  __GEN_FWD__
if errorlevel 1 exit /b 1
cmake --build . -- __KEEP__ __NSTATS__ -j__JOBS__
CMDEOF
# подстановка путей отдельным прогоном: в heredoc не смешивать $-расширения
python3 - "$CCT/$TC_TAG.cmd" "$BR/Src" "$BR" "$GEN" "$BUILD" "$CMAKE_DIR/bin" \
          "$NINJA" "$KEEP" "$NSTATS" "$JOBS" "$CL_PATH" "$CL_INC" "$CL_LIB" \
          "$LAUNCHER" <<'PY'
import os, sys
(p, src, r1117, gen, build, cmakedir, ninja,
 keep, nstats, jobs, clpath, clinc, cllib, launcher) = sys.argv[1:15]


def w(x):        # windows-путь для cmd (set / cd /d): wine требует букву диска
    return "Z:" + x.replace("/", "\\")


def f(x):        # путь для аргументов cmake (обратный слэш в CMake — escape)
    return "Z:" + x


s = open(p, encoding="utf-8").read()
launcher_flag = ("-DCMAKE_CXX_COMPILER_LAUNCHER=" + f(launcher)) if launcher else ""
for k, v in {"__CMAKE_WIN__": w(cmakedir), "__NINJADIR_WIN__": w(os.path.dirname(ninja)),
             "__CL_PATH__": clpath, "__CL_INC__": clinc, "__CL_LIB__": cllib,
             "__BUILD_WIN__": w(build), "__SRC_FWD__": f(src), "__R1117_FWD__": f(r1117),
             "__GEN_FWD__": f(gen), "__NINJA_FWD__": f(ninja),
             "__LAUNCHER__": launcher_flag,
             "__KEEP__": keep, "__NSTATS__": nstats, "__JOBS__": jobs}.items():
    s = s.replace(k, v)
open(p, "w", encoding="utf-8").write(s)
PY

if [ "$DO_GEN_ONLY" = 1 ]; then
    echo "== --gen-only: граф в $GEN, cmd-обёртка в $CCT/$TC_TAG.cmd"
    exit 0
fi

# ----------------------------------------- 2/3. конфигурирование + сборка (Wine)
zpath() { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }   # для cmd (/d, set)
zfwd()  { echo "Z:$1"; }                              # для аргументов cmake (обратный слэш — escape)
SRC_WIN="$(zpath "$BR/Src")"
R1117_WIN="$(zpath "$BR")"
GEN_WIN="$(zpath "$GEN")"
BUILD_WIN="$(zpath "$BUILD")"
CMAKE_WIN="$(zpath "$CMAKE_DIR/bin")"
NINJA_WIN="$(zpath "$NINJA")"

# ninja держит .ninja_lock; после kill/обрыва он остаётся и следующий запуск
# даёт «ninja: error: failed recompaction: Permission denied».
if [ -f "$BUILD/.ninja_lock" ] && ! pgrep -x ninja >/dev/null && ! pgrep -f "wintools/[n]inja" >/dev/null; then
    echo "== снимаю зависший .ninja_lock в $BUILD"
    rm -f "$BUILD/.ninja_lock"
fi
# CMakeCache хранит абсолютные пути gen-/build-каталогов: несовпадение = невалидный
# кэш. CMakeCache.txt пишется windows-cmake → CRLF: \r обязан быть снят, иначе
# сравнение всегда «разное» и build-каталог сносится каждый запуск.
if [ -f "$BUILD/CMakeCache.txt" ]; then
    cached_home="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" | tr -d '\r' | tr 'A-Z' 'a-z')"
    cached_dir="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD/CMakeCache.txt" | tr -d '\r' | tr 'A-Z' 'a-z')"
    want_home="$(echo "Z:$GEN" | tr 'A-Z' 'a-z')"
    want_dir="$(echo "Z:$BUILD" | tr 'A-Z' 'a-z')"
    if [ "$cached_home" != "$want_home" ] || [ "$cached_dir" != "$want_dir" ]; then
        echo "== кэш cmake создан при другом пути (gen=$cached_home build=$cached_dir) — сбрасываю $BUILD"
        rm -rf "$BUILD"; mkdir -p "$BUILD"
    fi
fi
if [ "$DO_CHECK" = 1 ]; then
    echo "== --check: сборка пропущена, проверяю артефакт"
else
    echo "== 2/3. cmake -G Ninja (configure) + ninja"
    WINEDEBUG=-all wine cmd /c "$(zpath "$CCT/$TC_TAG.cmd")"
fi
# ----------------------------------------------------------- 3. результат
EXE="$BUILD/PW_Game.exe"
if [ "$DO_KEEP" = 1 ] && [ ! -f "$EXE" ]; then
    echo "== 3/3. PW_Game.exe не собран (--keep-going): считается компиляция"
    python3 "$CCT/check_exe.py" "$BUILD" --count-obj
    exit 0
fi
[ -f "$EXE" ] || { echo "== 3/3. PW_Game.exe НЕ СОБРАН" >&2; exit 1; }
echo "== 3/3. PW_Game.exe:"
python3 "$CCT/check_exe.py" "$EXE" "$BR/Src/Version.h"
