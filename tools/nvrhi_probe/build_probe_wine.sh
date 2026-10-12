#!/usr/bin/env bash
# build_probe_wine.sh — конфигурирование и сборка nvrhi_probe под Wine тем же
# тулчейном, что и клиент (cl 14.44, Windows Kits 10, cmake.exe + ninja.exe).
#
#   ./build_probe_wine.sh            # configure + build
#   ./build_probe_wine.sh --clean    # снести build/
#   ./build_probe_wine.sh --run      # собрать и запустить под wine
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
CMAKE_DIR="${CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"

VS22_UNIX="drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
KITS_UNIX="drive_c/Program Files (x86)/Windows Kits/10"

[ -f "$CMAKE_DIR/bin/cmake.exe" ] || { echo "ERROR: нет cmake.exe: $CMAKE_DIR" >&2; exit 1; }
[ -f "$NINJA" ] || { echo "ERROR: нет ninja.exe: $NINJA" >&2; exit 1; }

MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
CL_PATH="$MSVC_WIN\\$MSVCVER\bin\HostX64\x64;$KITS_WIN\bin\x64"
CL_INC="$MSVC_WIN\\$MSVCVER\include;$KITS_WIN\Include\\${KVER}\ucrt;$KITS_WIN\Include\\${KVER}\um;$KITS_WIN\Include\\${KVER}\shared"
CL_LIB="$MSVC_WIN\\$MSVCVER\lib\x64;$KITS_WIN\Lib\\${KVER}\ucrt\x64;$KITS_WIN\Lib\\${KVER}\um\x64"
echo "== MSVC $MSVCVER, Windows Kits $KVER, префикс $WINEPREFIX"

RUN=0
for a in "$@"; do
  case "$a" in
    --clean) rm -rf "$BUILD" ;;
    --run)   RUN=1 ;;
  esac
done
mkdir -p "$BUILD"

w()    { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }
fwd()  { echo "Z:$1"; }

cat > "$HERE/_probe.cmd" <<CMDEOF
@echo off
set PATH=$(w "$CMAKE_DIR/bin");$(w "$(dirname "$NINJA")");$CL_PATH;%PATH%
set INCLUDE=$CL_INC
set LIB=$CL_LIB
set WINEDEBUG=-all
cd /d $(w "$BUILD")
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" ^
  -DCMAKE_MAKE_PROGRAM=$(fwd "$NINJA") $(fwd "$HERE")
if errorlevel 1 exit /b 1
cmake --build . -- -k0 -j$(nproc)
CMDEOF

export WINEPREFIX
WINEDEBUG=-all wine cmd /c "$(w "$HERE/_probe.cmd")" || exit 1

EXE="$BUILD/nvrhi_probe.exe"
if [ -f "$EXE" ]; then
  echo "== артефакт: $EXE ($(stat -c%s "$EXE") байт)"
  ls -l "$BUILD"/nvrhi/*.lib 2>/dev/null | awk '{print "   ", $5, $9}'
  [ "$RUN" = 1 ] && { echo "== запуск:"; WINEDEBUG=-all wine "$(fwd "$EXE")"; }
else
  echo "== nvrhi_probe.exe НЕ СОБРАН" >&2
  exit 1
fi
