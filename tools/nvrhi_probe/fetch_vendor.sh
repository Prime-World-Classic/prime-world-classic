#!/usr/bin/env bash
# fetch_vendor.sh — тянет зависимости nvrhi_probe (их нет в git: ~50 МБ чужого
# кода, решение о вендоринге NVRHI в дерево клиента принимается на этапе 3).
#
#   ./fetch_vendor.sh
#
# Версии зафиксированы — те же, что подтягивает CMake NVRHI по умолчанию
# (NVRHI_VULKAN_HEADERS_GIT_TAG / NVRHI_DIRECTX_HEADERS_GIT_TAG).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
V="$HERE/vendor"
mkdir -p "$V"

NVRHI_TAG=6b96fb03e07539f08327aea76c56d55f1de9d906
VK_TAG=v1.4.352
DX_TAG=v1.717.0-preview
LOADER_TAG=v1.4.352

clone() { # $1 url  $2 tag  $3 dir
  [ -d "$V/$3/.git" ] && { echo "уже есть: $3"; return; }
  git clone --depth 1 --branch "$2" "$1" "$V/$3"
}

clone https://github.com/KhronosGroup/Vulkan-Headers.git  "$VK_TAG" vulkan-headers
clone https://github.com/microsoft/DirectX-Headers.git    "$DX_TAG" directx-headers
clone https://github.com/KhronosGroup/Vulkan-Loader.git   "$LOADER_TAG" vulkan-loader

# Mesa для Windows (только ICD lavapipe) — software-Vulkan в tiny10, где нет
# драйверного Vulkan и вырезан системный vulkan-1.dll. Берём два файла:
# vulkan_lvp.dll + lvp_icd.x86_64.json. Загрузчик (vulkan-1.dll) — свой,
# собирается build_loader_wine.sh (в Mesa-сборке его нет).
MESA_VER=26.2.4
if [ ! -f "$V/mesa-win/vulkan_lvp.dll" ]; then
  echo "Mesa $MESA_VER: качаю release-msvc ($(printf %s "$MESA_VER") — ~70 МБ)"
  tmp="$(mktemp -d)"
  curl -L --retry 3 --max-time 900 -o "$tmp/mesa.7z" \
    "https://github.com/pal1000/mesa-dist-win/releases/download/$MESA_VER/mesa3d-$MESA_VER-release-msvc.7z"
  command -v 7z >/dev/null || { echo "ERROR: нужен p7zip (7z)" >&2; exit 1; }
  7z x -y -o"$tmp/out" "$tmp/mesa.7z" >/dev/null
  mkdir -p "$V/mesa-win"
  cp "$tmp/out/x64/vulkan_lvp.dll" "$tmp/out/x64/lvp_icd.x86_64.json" "$V/mesa-win/"
  rm -rf "$tmp"
fi

if [ ! -d "$V/nvrhi" ]; then
  tmp="$(mktemp -d)"
  git clone --quiet "$tmp/x" "$tmp/x" 2>/dev/null || true
  echo "NVRHI: клонирую $NVRHI_TAG"
  git clone --filter=blob:none https://github.com/NVIDIAGameWorks/nvrhi.git "$V/nvrhi"
  git -C "$V/nvrhi" checkout --quiet "$NVRHI_TAG"
  rm -rf "$V/nvrhi/.git"
fi
echo "$NVRHI_TAG" > "$V/nvrhi/SOURCE_COMMIT.txt"
echo "готово: $(ls "$V")"
