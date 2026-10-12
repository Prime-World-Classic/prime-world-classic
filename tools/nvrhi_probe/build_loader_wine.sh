#!/usr/bin/env bash
# build_loader_wine.sh — собирает загрузчик Vulkan (vulkan-1.dll) из
# vendor/vulkan-loader (KhronosGroup/Vulkan-Loader) тем же MSVC под Wine, что и
# клиент.
#
# Зачем: в tiny10 (Win10 LTSC 17763, урезанная) вырезан системный
# C:\Windows\System32\vulkan-1.dll, а Mesa-сборка (mesa-dist-win) несёт только
# ICD (vulkan_lvp.dll) — загрузчика у неё нет. Свой vulkan-1.dll кладём рядом с
# пробой в госте.
#
#   ./build_loader_wine.sh            # configure + build
#   ./build_loader_wine.sh --clean
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/vendor/vulkan-loader"
BUILD="$HERE/build-loader"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
CMAKE_DIR="${CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"

VS22_UNIX="drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
KITS_UNIX="drive_c/Program Files (x86)/Windows Kits/10"

[ -d "$SRC" ] || { echo "ERROR: нет $SRC — сначала ./fetch_vendor.sh" >&2; exit 1; }

MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
CL_PATH="$MSVC_WIN\\$MSVCVER\bin\HostX64\x64;$KITS_WIN\bin\x64"
CL_INC="$MSVC_WIN\\$MSVCVER\include;$KITS_WIN\Include\\${KVER}\ucrt;$KITS_WIN\Include\\${KVER}\um;$KITS_WIN\Include\\${KVER}\shared"
CL_LIB="$MSVC_WIN\\$MSVCVER\lib\x64;$KITS_WIN\Lib\\${KVER}\ucrt\x64;$KITS_WIN\Lib\\${KVER}\um\x64"
echo "== MSVC $MSVCVER, Windows Kits $KVER"

for a in "$@"; do [ "$a" = "--clean" ] && rm -rf "$BUILD"; done
mkdir -p "$BUILD"

w()   { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }
fwd() { echo "Z:$1"; }

# Загрузчик ищет VulkanHeaders через find_package(CONFIG) — ставим наши
# вендорные заголовки и скармливаем ему путь к конфиг-пакету.
cat > "$HERE/_loader.cmd" <<CMDEOF
@echo off
set PATH=$(w "$CMAKE_DIR/bin");$(w "$(dirname "$NINJA")");$CL_PATH;%PATH%
set INCLUDE=$CL_INC
set LIB=$CL_LIB
set WINEDEBUG=-all
cmake -G Ninja -S $(fwd "$HERE/vendor/vulkan-headers") -B $(fwd "$BUILD/vk-headers") ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  -DCMAKE_INSTALL_PREFIX=$(fwd "$BUILD/install")
if errorlevel 1 exit /b 1
cmake --install $(fwd "$BUILD/vk-headers")
if errorlevel 1 exit /b 1
cmake -G Ninja -S $(fwd "$SRC") -B $(fwd "$BUILD/loader") ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -DUPDATE_DEPS=OFF ^
  -DLOADER_CODEGEN=OFF -DENABLE_WIN10_ONECORE=OFF ^
  -DVulkanHeaders_DIR=$(fwd "$BUILD/install/share/cmake/VulkanHeaders")
if errorlevel 1 exit /b 1
cmake --build $(fwd "$BUILD/loader") -- -k0 -j$(nproc)
CMDEOF

export WINEPREFIX
WINEDEBUG=-all wine cmd /c "$(w "$HERE/_loader.cmd")" || exit 1

DLL="$(find "$BUILD/loader" -name 'vulkan-1.dll' 2>/dev/null | head -1)"
if [ -n "$DLL" ]; then
  echo "== артефакт: $DLL ($(stat -c%s "$DLL") байт)"
else
  echo "== vulkan-1.dll НЕ СОБРАН" >&2
  exit 1
fi
