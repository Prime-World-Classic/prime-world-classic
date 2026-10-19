#!/usr/bin/env bash
# build_harness.sh — сборка тестовой обвязки VM формул.
#   ./build_harness.sh [--toolchain=vs2022|vs2008]
# vs2022 -> x64 (только VM), vs2008 -> x86 (VM + предкомпилированный код, если
# включён HARNESS_NATIVE).
set -euo pipefail
TOOLCHAIN="vs2022"
for a in "$@"; do case "$a" in --toolchain=*) TOOLCHAIN="${a#--toolchain=}";; esac; done

HERE="$(cd "$(dirname "$0")" && pwd)"
R1117="$(cd "$HERE/../../.." && pwd)"

FE="/Fe:vm_harness.exe"
VS9_UNIX='drive_c/Program Files (x86)/Microsoft Visual Studio 9.0'
SDK9_UNIX='drive_c/Program Files/Microsoft SDKs/Windows/v6.0A'
VS22_UNIX='drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools'
KITS_UNIX='drive_c/Program Files (x86)/Windows Kits/10'

case "$TOOLCHAIN" in
  vs2008)
    WINEPREFIX="${WINEPREFIX:-$HOME/pwbuild/wine32}"
    CL_PATH="C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\bin;C:\\Program Files\\Microsoft SDKs\\Windows\\v6.0A\\Bin"
    CL_INC="C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\include;C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\atlmfc\\include;C:\\Program Files\\Microsoft SDKs\\Windows\\v6.0A\\Include"
    CL_LIB="C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\lib;C:\\Program Files\\Microsoft SDKs\\Windows\\v6.0A\\Lib"
    DEFAULT_OPTS="/WX- /MT"
    FE="/Fevm_harness.exe"
    ;;
  vs2022)
    WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
    MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
    KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
    MSVC_WIN="C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\$MSVCVER"
    KITS_WIN="C:\\Program Files (x86)\\Windows Kits\\10"
    CL_PATH="$MSVC_WIN\\bin\\HostX64\\x64;$KITS_WIN\\bin\\x64"
    CL_INC="$MSVC_WIN\\include;$KITS_WIN\\Include\\$KVER\\ucrt;$KITS_WIN\\Include\\$KVER\\um;$KITS_WIN\\Include\\$KVER\\shared"
    CL_LIB="$MSVC_WIN\\lib\\x64;$KITS_WIN\\Lib\\$KVER\\ucrt\\x64;$KITS_WIN\\Lib\\$KVER\\um\\x64"
    DEFAULT_OPTS="/WX- /MT"
    ;;
  *) echo "тулчейн: vs2008|vs2022" >&2; exit 1 ;;
esac

zpath() { python3 -c 'import sys;print("Z:"+sys.argv[1].replace("/","\\\\"))' "$1"; }
R1117W="$(zpath "$R1117")"
HEREW="$(zpath "$HERE")"

export WINEPREFIX WINEDEBUG=-all
mkdir -p "$HERE/obj-$TOOLCHAIN"

# mock-интерфейсы генерируются из FormulaPars.h — без них обвязка не собирается
if [ ! -f "$HERE/MockFormulaPars.h" ]; then
  python3 "$HERE/../gen_accessors.py" --mock > "$HERE/MockFormulaPars.h"
fi

EXTRA_SRC=""
EXTRA_DEF=""
if [ "${HARNESS_REF:-0}" = "1" ]; then
  # имена относительные: cmd делает cd /d $HEREW
  EXTRA_SRC="$(cd "$HERE" && ls ref_*.cpp | tr '\n' ' ')"
  EXTRA_DEF="/DHARNESS_REF"
fi
if [ "$TOOLCHAIN" = "vs2008" ] && [ "${HARNESS_NATIVE:-0}" = "1" ]; then
  EXTRA_SRC="native_exec.cpp $R1117W\\Src\\System\\DataExecutor.cpp $R1117W\\Src\\System\\ExecutionMemoryManager.cpp $R1117W\\Src\\System\\Base64.cpp $R1117W\\Src\\System\\MemoryStream.cpp $R1117W\\Src\\System\\Stream.cpp"
  EXTRA_DEF="/DHARNESS_NATIVE"
fi

cat > "$HERE/cmd-$TOOLCHAIN.cmd" <<CMDEOF
@echo off
set PATH=$CL_PATH;%PATH%
set INCLUDE=$CL_INC;$R1117W\\Src\\System;$R1117W\\Src;$R1117W\\Data\\GameLogic
set LIB=$CL_LIB
set WINEDEBUG=-all
cd /d $HEREW
cl /nologo $DEFAULT_OPTS /O2 /W3 /DWIN32 /DNDEBUG /D_SHIPPING /D_CRT_SECURE_NO_WARNINGS $EXTRA_DEF \
   vm_harness.cpp "$R1117W\\Src\\System\\FormulaVM.cpp" $EXTRA_SRC \
   /Fo$HEREW\\obj-$TOOLCHAIN\\ $FE
echo rc=%ERRORLEVEL%
CMDEOF

wine cmd /c "$(zpath "$HERE/cmd-$TOOLCHAIN.cmd")"
ls -la "$HERE/vm_harness.exe" 2>/dev/null || true
