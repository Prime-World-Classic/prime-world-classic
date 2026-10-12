#!/usr/bin/env bash
# run.sh — собрать и запустить sh_math_test под Wine на настоящем MS
# d3dx9_43.dll (Vendor/DirectX/bin), а не на builtin-реализации Wine.
#
#   ./run.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
R1117="$(cd "$HERE/../../pw/branches/r1117" && pwd)"
BUILD="$HERE/build"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
CMAKE_DIR="${CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"

VS22_UNIX="drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
KITS_UNIX="drive_c/Program Files (x86)/Windows Kits/10"
MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
CL_PATH="$MSVC_WIN\\$MSVCVER\bin\HostX64\x64;$KITS_WIN\bin\x64"
CL_INC="$MSVC_WIN\\$MSVCVER\include;$KITS_WIN\Include\\${KVER}\ucrt;$KITS_WIN\Include\\${KVER}\um;$KITS_WIN\Include\\${KVER}\shared"
CL_LIB="$MSVC_WIN\\$MSVCVER\lib\x64;$KITS_WIN\Lib\\${KVER}\ucrt\x64;$KITS_WIN\Lib\\${KVER}\um\x64"

mkdir -p "$BUILD"
w()   { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }
fwd() { echo "Z:$1"; }

# Нужен НАСТОЯЩИЙ MS d3dx9_43.dll именно x64 (в дереве лежит только x86 —
# Vendor/DirectX/bin/d3dx9_43.dll это PE32). x64 берём из DirectX End-User
# Runtime (JUN2010_d3dx9_43_x64): распакованный каталог с d3dx9_43.dll +
# *.inf/*.cat. Кладём рядом с exe и ставим WINEDLLOVERRIDES=n, иначе Wine
# подставит свой builtin (его математика — не эталон).
DXRED="${DXRED:-/tmp/dxred/x64}"
[ -f "$DXRED/d3dx9_43.dll" ] || { echo "ERROR: нет x64 d3dx9_43.dll в $DXRED (DXRED=<каталог>)" >&2; exit 1; }
cp -f "$DXRED/d3dx9_43.dll" "$BUILD/"

cat > "$HERE/_build.cmd" <<CMDEOF
@echo off
set PATH=$CL_PATH;%PATH%
set INCLUDE=$CL_INC;$(w "$R1117")
set LIB=$CL_LIB;$(w "$R1117/Vendor/DirectX/Lib/x64")
cd /d $(w "$BUILD")
cl /nologo /W3 /O2 /MT /EHsc /Fe:sh_math_test.exe $(fwd "$HERE/sh_math_test.cpp") d3dx9.lib user32.lib
CMDEOF

export WINEPREFIX
WINEDEBUG=-all wine cmd /c "$(w "$HERE/_build.cmd")" || exit 1
[ -f "$BUILD/sh_math_test.exe" ] || { echo "НЕ СОБРАНО"; exit 1; }

echo "== запуск (d3dx9_43=n -> native MS DLL):"
WINEDLLOVERRIDES="d3dx9_43=n" WINEDEBUG=-all wine "$(fwd "$BUILD/sh_math_test.exe")"
