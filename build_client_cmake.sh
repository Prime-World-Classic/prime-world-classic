#!/usr/bin/env bash
# =============================================================================
# build_client_cmake.sh — сборка клиента через CMake (этап 1 плана
# PLAN_client_modern.md). Комплятор тот же, что у эталонного пути
# (build_client_wine.sh: VS2008 SP1 x86 под Wine) — меняется ТОЛЬКО система
# сборки, чтобы проверить корректность генерируемого графа
# (Tools/ClientCMake/gen_cmake.py), а не кода/тулчейна.
#
# Граф строится из той же model.json (parse_vcproj.py), что и у драйвера.
# Проекты — OBJECT-библиотеки: в линковку попадают ВСЕ объекты (статические
# .lib теряют TU ради статических регистраций — см. gen_cmake.py).
#
# Использование:
#   ./build_client_cmake.sh                 # конфигурирование + сборка
#   ./build_client_cmake.sh --clean         # удалить build-каталог
#   ./build_client_cmake.sh --gen-only      # только сгенерировать CMakeLists
#   ./build_client_cmake.sh --prefix=PATH   # другой Wine-префикс
#   ./build_client_cmake.sh --toolchain=vs2008|vs2022   # компилятор (см. ниже)
#   ./build_client_cmake.sh --keep-going    # ninja -k0: не падать на первой
#       ошибке и не требовать exe (обзор ошибок компиляции, линковка может
#       упасть на вендорных x86-либах)
#   PW_BUILD_JOBS=8 ./build_client_cmake.sh
#   PW_EXTRA_OPTS="/std:c++17 /WX-"  — доп. флаги cl во все TU (как в
#       build_client_wine.sh); PW_MACHINE=X86|X64 — /MACHINE у линкера.
#
# Тулчейны:
#   vs2008 (по умолчанию) — эталон: cl 15.00, x86, Windows SDK v6.0A,
#       префикс ~/pwbuild/wine32. build-каталог Tools/ClientCMake/build/vs2008/.
#   vs2022 — современный: cl 14.44 + Windows Kits 10, префикс ~/.wine-vs.
#       ВНИМАНИЕ: в этом префиксе установлен ТОЛЬКО x64-набор (bin\HostX64\x64,
#       lib\x64) — x86-мишень им не строится, «cl 14.44 + x86» требует установки
#       компонента x64/x86-компилятора. Мишень по умолчанию X64.
#
# Требования: Wine-префикс с VS2008 SP1 + Windows SDK v6.0A (как у
# build_client_wine.sh) + Windows cmake.exe и ninja.exe:
#   ~/pwbuild/wintools/cmake-3.31.6-windows-x86_64/bin/cmake.exe
#   ~/pwbuild/wintools/ninja.exe
# (переменные PW_CMAKE_DIR / PW_NINJA переопределяют).
#
# Почему именно такие флаги конфигурирования (все три — обход ограничений Wine,
# проверено эмпирически):
#   -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=  : cl под wine не может писать PDB
#       (fatal C1902 "Program database manager mismatch"); CMake по умолчанию
#       добавляет /Zi + /Fd.
#   -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO   : mt.exe /outputresource под wine
#       падает (access violation). Манифест вместо этого — rc-ресурс 1 24,
#       который генерирует gen_cmake.py (как wine_app.manifest в драйвере).
#   -DCMAKE_TRY_COMPILE_CONFIGURATION=Release: try-compile CMake иначе идёт с
#       Debug-флагами (/Zi ...) и падает на том же C1902.
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
BR="$REPO/pw/branches/r1117"
WCB="$BR/Tools/WineClientBuild"
CCT="$BR/Tools/ClientCMake"
CFG="${PW_CFG:-ShippingSingleExe|Win32}"
TOOLCHAIN="${PW_TOOLCHAIN:-vs2008}"
WINEPREFIX="${WINEPREFIX:-}"
JOBS="${PW_BUILD_JOBS:-8}"
CMAKE_DIR="${PW_CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"

DO_CLEAN=0; DO_GEN_ONLY=0; DO_KEEP=0; PREFIX_FORCED=0
for arg in "$@"; do
    case "$arg" in
        --prefix=*) WINEPREFIX="${arg#--prefix=}"; PREFIX_FORCED=1 ;;
        --toolchain=*) TOOLCHAIN="${arg#--toolchain=}" ;;
        --clean)    DO_CLEAN=1 ;;
        --gen-only) DO_GEN_ONLY=1 ;;
        --keep-going) DO_KEEP=1 ;;
        -h|--help)  grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -40; exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done

# каталоги сборки — по тулчейну: CMakeCache хранит компилятор, в одном build-
# каталоге два тулчейна не живут (и .obj от cl 15 не должны смешиваться с cl 14)
BUILD="$CCT/build/$TOOLCHAIN"
GEN="$CCT/gen/$TOOLCHAIN"
KEEP=""
if [ "$DO_KEEP" = 1 ]; then KEEP="-k0"; fi

# ------------------------------------------------------------- 0. Wine-префикс
# vs2008 — эталонный путь (cl 15.00, x86, SDK v6.0A);
# vs2022 — современный (cl 14.44, ТОЛЬКО x64-мишень: в ~/.wine-vs стоит
#          HostX64/x64 и lib/x64, x86-целевого компилятора нет).
VS9_UNIX='drive_c/Program Files (x86)/Microsoft Visual Studio 9.0'
SDK9_UNIX='drive_c/Program Files/Microsoft SDKs/Windows/v6.0A'
VS22_UNIX='drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools'
KITS_UNIX='drive_c/Program Files (x86)/Windows Kits/10'

prefix_ok() {
    local p="$1"
    [ -d "$p" ] || return 1
    case "$TOOLCHAIN" in
      vs2008)
        [ -x "$p/$VS9_UNIX/VC/bin/cl.exe" ] || return 1
        [ -d "$p/$SDK9_UNIX/Include" ] || return 1
        [ -d "$p/$SDK9_UNIX/Lib" ] || return 1 ;;
      vs2022)
        ls "$p/$VS22_UNIX/VC/Tools/MSVC/"*/bin/HostX64/x64/cl.exe >/dev/null 2>&1 || return 1
        ls -d "$p/$KITS_UNIX/Include/"10.* >/dev/null 2>&1 || return 1 ;;
      *) echo "ERROR: неизвестный тулчейн $TOOLCHAIN (vs2008|vs2022)" >&2; return 1 ;;
    esac
    return 0
}
if [ "$PREFIX_FORCED" != 1 ]; then
    if [ -z "$WINEPREFIX" ]; then
        case "$TOOLCHAIN" in
          vs2008) WINEPREFIX="$HOME/pwbuild/wine32" ;;
          vs2022) WINEPREFIX="$HOME/.wine-vs" ;;
        esac
    fi
    if ! prefix_ok "$WINEPREFIX"; then
        for cand in "$HOME/pwbuild/wine32" "$HOME/.wine-vs" "$HOME/.wine-vs2008" "$HOME/.wine" \
                    $(ls -d "$HOME"/*/wine* "$HOME"/.wine* 2>/dev/null); do
            if prefix_ok "$cand"; then WINEPREFIX="$cand"; break; fi
        done
    fi
fi
prefix_ok "$WINEPREFIX" || { echo "ERROR: в префиксе '$WINEPREFIX' нет тулчейна $TOOLCHAIN" >&2; exit 1; }
echo "== Wine-префикс: $WINEPREFIX (тулчейн $TOOLCHAIN)"
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
fi

# ------------------------------------------- 0c. тулчейн (до генерации!)
# PW_MACHINE читает gen_cmake.py (/MACHINE у линкера и CRT-зависимость
# манифеста), поэтому блок обязан стоять ДО шага 1.
case "$TOOLCHAIN" in
  vs2008)
    VC_WIN='C:\Program Files (x86)\Microsoft Visual Studio 9.0'
    SDK_WIN='C:\Program Files\Microsoft SDKs\Windows\v6.0A'
    CL_PATH="$VC_WIN\VC\bin;$SDK_WIN\Bin"
    CL_INC="$VC_WIN\VC\include;$VC_WIN\VC\atlmfc\include;$SDK_WIN\Include"
    CL_LIB="$VC_WIN\VC\lib;$VC_WIN\VC\atlmfc\lib;$SDK_WIN\Lib"
    : "${PW_MACHINE:=X86}"
    ;;
  vs2022)
    # версия MSVC и SDK берутся из префикса (в префиксе сейчас 14.44.35207 +
    # Windows Kits 10.0.26100.0)
    MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
    KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
    MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
    KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
    MSVC_WIN="$MSVC_WIN\\$MSVCVER"
    # x64-мишень: cl/link из bin\HostX64\x64, либы lib\x64 + Kits um/ucrt x64
    CL_PATH="$MSVC_WIN\bin\HostX64\x64;$KITS_WIN\bin\x64"
    CL_INC="$MSVC_WIN\include;$KITS_WIN\Include\\${KVER}\ucrt;$KITS_WIN\Include\\${KVER}\um;$KITS_WIN\Include\\${KVER}\shared"
    CL_LIB="$MSVC_WIN\lib\x64;$KITS_WIN\Lib\\${KVER}\ucrt\x64;$KITS_WIN\Lib\\${KVER}\um\x64"
    echo "== MSVC $MSVCVER, Windows Kits $KVER"
    : "${PW_MACHINE:=X64}"
    # /WX из vcproj с cl 14.44 убивает сборку на новых предупреждениях
    # (C4458 hides class member, C4244/C4267 сужения на x64 — их ~4,5 тыс.).
    # Для нового тулчейна /WX снимаем по умолчанию: сначала компиляция и
    # линковка, аудит предупреждений — отдельным проходом (C4244/C4267 на x64
    # = сигнал о 64-битных сужениях, их разбирать списком, а не молча гасить).
    : "${PW_EXTRA_OPTS:=/WX-}"
    # comsuppw.lib/comsuppwd.lib удалены из VC начиная с VS2017 15.3 — _com_error и
    # _com_ptr_t переехали в <comdef.h>, линковать их нечем. vcproj их всё ещё
    # просит → снимаем только для нового тулчейна.
    : "${PW_DROP_LIBS:=comsuppw comsuppwd}"
    ;;
esac
export PW_MACHINE
export PW_EXTRA_OPTS
export PW_DROP_LIBS
echo "== мишень: /MACHINE:$PW_MACHINE"
echo "== доп. флаги cl: ${PW_EXTRA_OPTS:-<нет>}"
[ -n "$PW_DROP_LIBS" ] && echo "== снимаемые из линковки .lib: $PW_DROP_LIBS"

# ------------------------------------------------------- 1. модель + генерация
echo "== 1/3. model.json + генерация CMake"
python3 "$WCB/parse_vcproj.py" "$BR/Src" "$CCT/model.json" "$CFG" >/dev/null
[ "$DO_CLEAN" = 1 ] && rm -rf "$BUILD"
rm -rf "$GEN"
python3 "$CCT/gen_cmake.py" "$BR/Src" "$CCT/model.json" "$GEN"
[ "$DO_GEN_ONLY" = 1 ] && exit 0

# ------------------------------------------- 2/3. конфигурирование + сборка (Wine)
# wine-пути: Z: корень = / (см. build_client.py winpath)
zpath() { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }
# CMake-аргументы: прямые слебы. В CMake-строке обратный слэш — начало escape
# последовательности ("Z:\home" -> "Invalid character escape '\h').
zfwd() { echo "Z:$1"; }
SRC_WIN="$(zpath "$BR/Src")"
R1117_WIN="$(zpath "$BR")"
GEN_WIN="$(zpath "$GEN")"
BUILD_WIN="$(zpath "$BUILD")"
CMAKE_WIN="$(zpath "$CMAKE_DIR/bin")"
NINJA_WIN="$(zpath "$NINJA")"
SRC_FWD="$(zfwd "$BR/Src")"
R1117_FWD="$(zfwd "$BR")"
GEN_FWD="$(zfwd "$GEN")"
NINJA_FWD="$(zfwd "$NINJA")"

# компиляторное окружение (CL_PATH/CL_INC/CL_LIB/PW_MACHINE) задано в 0c выше
mkdir -p "$BUILD"
# ninja держит .ninja_lock; после kill/обрыва он остаётся и следующий запуск
# даёт «ninja: error: failed recompaction: Permission denied» (cmake generate
# падает). Снимать можно только когда параллельной сборки в этом каталоге нет.
if [ -f "$BUILD/.ninja_lock" ] && ! pgrep -x ninja >/dev/null && ! pgrep -f "wintools/ninja" >/dev/null; then
    echo "== снимаю зависший .ninja_lock в $BUILD"
    rm -f "$BUILD/.ninja_lock"
fi
# CMakeCache хранит абсолютные пути build- и gen-каталогов: перенос каталога под
# тулчейн (build/ -> build/vs2008/) делает старый кэш невалидным — configure
# падает («CMakeCache.txt directory ... is different than ...»). Как в
# build_server.sh: несовпадение пути = сброс build-каталога.
if [ -f "$BUILD/CMakeCache.txt" ]; then
    # CMakeCache.txt пишется windows-cmake → CRLF: \r обязан быть снят, иначе
    # сравнение всегда «разное» и build-каталог сносится каждый запуск
    cached_home="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" | tr -d '\r' | tr 'A-Z' 'a-z')"
    cached_dir="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD/CMakeCache.txt" | tr -d '\r' | tr 'A-Z' 'a-z')"
    want_home="$(echo "Z:$GEN" | tr 'A-Z' 'a-z')"
    want_dir="$(echo "Z:$BUILD" | tr 'A-Z' 'a-z')"
    if [ "$cached_home" != "$want_home" ] || [ "$cached_dir" != "$want_dir" ]; then
        echo "== кэш cmake создан при другом пути (gen=$cached_home build=$cached_dir) — сбрасываю $BUILD"
        rm -rf "$BUILD"; mkdir -p "$BUILD"
    fi
fi
cat > "$CCT/wine_env.cmd" <<EOF
@echo off
set PATH=$CMAKE_WIN;$(zpath "$(dirname "$NINJA")");$CL_PATH;%PATH%
set INCLUDE=$CL_INC
set LIB=$CL_LIB
set WINEDEBUG=-all
cd /d $BUILD_WIN
cmake -G Ninja -DPW_SRC=$SRC_FWD -DR1117=$R1117_FWD ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" ^
  -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO ^
  -DCMAKE_MAKE_PROGRAM=$NINJA_FWD ^
  $GEN_FWD
if errorlevel 1 exit /b 1
cmake --build . -- $KEEP -j$JOBS
EOF

echo "== 2/3. cmake -G Ninja (configure)"
WINEDEBUG=-all wine cmd /c "$(zpath "$CCT/wine_env.cmd")"

# ----------------------------------------------------------- 3. результат
EXE="$BUILD/PW_Game.exe"
if [ "$DO_KEEP" = 1 ] && [ ! -f "$EXE" ]; then
    echo "== 3/3. PW_Game.exe не собран (--keep-going): считается компиляция"
    find "$BUILD/CMakeFiles" -name '*.obj' | wc -l | sed 's/^/   скомпилировано .obj: /'
    exit 0
fi
if [ -f "$EXE" ]; then
    echo "== 3/3. PW_Game.exe: $(stat -c%s "$EXE") байт"
    file "$EXE" | sed 's/.*, //'
    python3 - "$EXE" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
mach, nsec = struct.unpack_from('<HH', d, pe + 4)
chars = struct.unpack_from('<H', d, pe + 22)[0]
sizeimg = struct.unpack_from('<I', d, pe + 56)[0]
print(f"machine=0x{mach:04x} ({'x86' if mach==0x14c else 'x64' if mach==0x8664 else '?'}) "
      f"sections={nsec} Characteristics=0x{chars:04x} (LAA={'да' if chars & 0x20 else 'нет'}) sizeOfImage={sizeimg}")
PY
else
    echo "== 3/3. PW_Game.exe НЕ СОБРАН" >&2
    exit 1
fi
