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
#   PW_BUILD_JOBS=8 ./build_client_cmake.sh
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
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs2008}"
JOBS="${PW_BUILD_JOBS:-8}"
CMAKE_DIR="${PW_CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"
BUILD="$CCT/build"
GEN="$CCT/gen"

DO_CLEAN=0; DO_GEN_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --prefix=*) WINEPREFIX="${arg#--prefix=}"; PREFIX_FORCED=1 ;;
        --clean)    DO_CLEAN=1 ;;
        --gen-only) DO_GEN_ONLY=1 ;;
        -h|--help)  grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -40; exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done
PREFIX_FORCED="${PREFIX_FORCED:-0}"

# ------------------------------------------------------------- 0. Wine-префикс
prefix_ok() {
    local p="$1"
    [ -d "$p" ] || return 1
    [ -x "$p/drive_c/Program Files (x86)/Microsoft Visual Studio 9.0/VC/bin/cl.exe" ] || return 1
    [ -d "$p/drive_c/Program Files/Microsoft SDKs/Windows/v6.0A/Include" ] || return 1
    [ -d "$p/drive_c/Program Files/Microsoft SDKs/Windows/v6.0A/Lib" ] || return 1
    return 0
}
if [ "$PREFIX_FORCED" != 1 ] && ! prefix_ok "$WINEPREFIX"; then
    for cand in "$HOME/pwbuild/wine32" "$HOME/.wine-vs2008" "$HOME/.wine" \
                $(ls -d "$HOME"/*/wine* "$HOME"/.wine* 2>/dev/null); do
        if prefix_ok "$cand"; then WINEPREFIX="$cand"; break; fi
    done
fi
prefix_ok "$WINEPREFIX" || { echo "ERROR: нет Wine-префикса с VS2008 SP1 + SDK v6.0A" >&2; exit 1; }
echo "== Wine-префикс: $WINEPREFIX"
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

VC_WIN='C:\Program Files (x86)\Microsoft Visual Studio 9.0'
SDK_WIN='C:\Program Files\Microsoft SDKs\Windows\v6.0A'

mkdir -p "$BUILD"
cat > "$CCT/wine_env.cmd" <<EOF
@echo off
set PATH=$CMAKE_WIN;$(zpath "$(dirname "$NINJA")");$VC_WIN\VC\bin;$SDK_WIN\Bin;%PATH%
set INCLUDE=$SDK_WIN\Include;$VC_WIN\VC\include;$VC_WIN\VC\atlmfc\include
set LIB=$VC_WIN\VC\lib;$VC_WIN\VC\atlmfc\lib;$SDK_WIN\Lib
set WINEDEBUG=-all
cd /d $BUILD_WIN
cmake -G Ninja -DPW_SRC=$SRC_FWD -DR1117=$R1117_FWD ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" ^
  -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO ^
  -DCMAKE_MAKE_PROGRAM=$NINJA_FWD ^
  $GEN_FWD
if errorlevel 1 exit /b 1
cmake --build . -- -j$JOBS
EOF

echo "== 2/3. cmake -G Ninja (configure)"
WINEDEBUG=-all wine cmd /c "$(zpath "$CCT/wine_env.cmd")"

# ----------------------------------------------------------- 3. результат
EXE="$BUILD/PW_Game.exe"
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
