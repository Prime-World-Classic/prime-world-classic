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

clone() { # $1 url  $2 tag  $3 dir
  [ -d "$V/$3/.git" ] && { echo "уже есть: $3"; return; }
  git clone --depth 1 --branch "$2" "$1" "$V/$3"
}

clone https://github.com/KhronosGroup/Vulkan-Headers.git  "$VK_TAG" vulkan-headers
clone https://github.com/microsoft/DirectX-Headers.git    "$DX_TAG" directx-headers

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
