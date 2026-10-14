#!/usr/bin/env bash
# =============================================================================
# fetch_render_deps.sh — зависимости рендер-бэкенда (PLAN_NVRHI.md, этап 3).
#
# Забирает в этот каталог ЧЕТЫРЕ дерева в зафиксированных версиях:
#
#   NVRHI/            NVIDIA NVRHI            6b96fb03e07539f08327aea76c56d55f1de9d906
#   Vulkan-Headers/   Khronos                 v1.4.352
#   DirectX-Headers/  Microsoft               v1.717.0-preview
#   SDL3/             SDL 3.2 LTS-линия       release-3.2.30
#
# Версии те же, что подтягивает CMake NVRHI по умолчанию
# (NVRHI_VULKAN_HEADERS_GIT_TAG / NVRHI_DIRECTX_HEADERS_GIT_TAG) — см.
# Vendor/NVRHI/CMakeLists.txt; сверять при апгрейде NVRHI.
#
# Почему свои, а не FetchContent: сборка клиента идёт без сети (Wine + cl), а
# FetchContent в NVRHI тянет хэдеры из git на этапе configure. Плюс хэдеры
# нужны и нашему коду (VkSurfaceKHR через SDL).
#
# Дерево режется до того, что нужно сборке: у SDL3 это test/ (21 МБ) и
# examples/ (8 МБ) + IDE-каталоги, у хэдеров — registry/tests/CI. Полные
# версии — в SOURCE_COMMIT.txt (там же URL).
#
#   ./fetch_render_deps.sh            # докачать/обновить отсутствующее
#   ./fetch_render_deps.sh --force    # перескачать всё заново
#
# После этого граф клиента собирает вендорские цели отдельной целью
# `render_vendor` (см. Tools/ClientCMake/gen_cmake.py, RENDER_VENDOR).
# =============================================================================
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
FORCE=0
for a in "$@"; do [ "$a" = "--force" ] && FORCE=1; done

NVRHI_SHA=6b96fb03e07539f08327aea76c56d55f1de9d906
VKH_TAG=v1.4.352
DXH_TAG=v1.717.0-preview
SDL_TAG=release-3.2.30

mark() { # $1=dir $2=url $3=ref
  cat > "$HERE/$1/SOURCE_COMMIT.txt" <<EOF
# вендоринг: не править руками, обновлять ./fetch_render_deps.sh --force
url:  $2
ref:  $3
cut:  $(date -u +%Y-%m-%d)
EOF
}

fresh() { # $1=dir — нужно ли качать
  [ "$FORCE" = 1 ] && { rm -rf "$HERE/$1"; return 0; }
  [ -d "$HERE/$1" ] && { echo "уже есть: $1"; return 1; }
  return 0
}

# Качаем archive-архив, а не git clone: git-протокол к github в этом окружении
# обрывается на середине (проверено: blob:none-клон nvrhi падает на fetch
# блобов), а метаданные git этим сборкам не нужны — у NVRHI версия задана в
# project(nvrhi VERSION 1.0.0), у SDL — в VERSION.txt.
# Имя корневого каталога внутри архива НЕ угадывается: GitHub пишет его по
# регистру репозитория и срезает 'v' у тегов (NVRHI-<sha>, Vulkan-Headers-1.4.352).
get_archive() { # $1=url — кладёт распакованный корень в $ARCH
  ARCH="$(mktemp -d)"
  curl -sL --retry 5 --retry-delay 3 --max-time 900 -o "$ARCH/a.tar.gz" "$1" || return 1
  tar xzf "$ARCH/a.tar.gz" -C "$ARCH" || return 1
  local d
  d="$(find "$ARCH" -mindepth 1 -maxdepth 1 -type d ! -name '.*' | head -1)"
  [ -d "$d" ] || return 1
  ARCH="$d"
}

# Берём archive-архив, а не git clone: git-протокол к github в этом окружении
# обрывается на середине (проверено: blob:none-клон nvrhi падает на fetch
# блобов), а метаданные git этим сборкам не нужны — у NVRHI версия задана
# в project(nvrhi VERSION 1.0.0), у SDL — в VERSION.txt.

# --- NVRHI ------------------------------------------------------------------
# Только include/ + src/ + cmake-обвязка: examples/ и doc/ в этом коммите
# ссылаются на отдельное дерево nvrhi-samples, нам они не нужны.
if fresh NVRHI; then
  echo "NVRHI: $NVRHI_SHA"
  get_archive "https://github.com/NVIDIAGameWorks/nvrhi/archive/$NVRHI_SHA.tar.gz" || { echo "ERROR: NVRHI" >&2; exit 1; }
  mkdir -p "$HERE/NVRHI"
  cp -a "$ARCH/." "$HERE/NVRHI/"
  rm -rf "$HERE/NVRHI/examples" "$HERE/NVRHI/doc" "$HERE/NVRHI/.github" "$HERE/NVRHI/a.tar.gz"
  rm -rf "$ARCH"
  mark NVRHI https://github.com/NVIDIAGameWorks/nvrhi.git "$NVRHI_SHA"
fi

# --- Vulkan-Headers ---------------------------------------------------------
# Нужен только include/ (vulkan.hpp целиком: NVRHI работает через Vulkan-Hpp,
# см. tools/nvrhi_probe/README.md — диспетчер Hpp инициализирует хост).
if fresh Vulkan-Headers; then
  echo "Vulkan-Headers: $VKH_TAG"
  get_archive "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/$VKH_TAG.tar.gz" || { echo "ERROR: Vulkan-Headers" >&2; exit 1; }
  mkdir -p "$HERE/Vulkan-Headers"
  cp -a "$ARCH/include" "$ARCH/LICENSE.md" "$ARCH/LICENSES" "$HERE/Vulkan-Headers/"
  rm -rf "$ARCH"
  mark Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git "$VKH_TAG"
fi

# --- DirectX-Headers --------------------------------------------------------
# Нужен include/ + CMakeLists (мишень DirectX::dxguid и т.п.) + cmake/.
if fresh DirectX-Headers; then
  echo "DirectX-Headers: $DXH_TAG"
  get_archive "https://github.com/microsoft/DirectX-Headers/archive/refs/tags/$DXH_TAG.tar.gz" || { echo "ERROR: DirectX-Headers" >&2; exit 1; }
  mkdir -p "$HERE/DirectX-Headers"
  cp -a "$ARCH/include" "$ARCH/src" "$ARCH/cmake" "$ARCH/CMakeLists.txt" "$ARCH/LICENSE" "$HERE/DirectX-Headers/"
  rm -rf "$ARCH"
  mark DirectX-Headers https://github.com/microsoft/DirectX-Headers.git "$DXH_TAG"
fi

# --- SDL3 (окно, Vulkan surface, ресайз; решение — §3.x GAP: swapchain) -----
# Режем test/ (21 МБ), examples/ (8 МБ), IDE-проекты и Android/Xcode: сборке
# нужны src/, include/, cmake/, CMakeLists.txt. wayland-protocols остаётся —
# его требует wayland-бэкенд SDL (Linux-контур после этапа 4).
if fresh SDL3; then
  echo "SDL3: $SDL_TAG"
  get_archive "https://github.com/libsdl-org/SDL/archive/refs/tags/$SDL_TAG.tar.gz" || { echo "ERROR: SDL3" >&2; exit 1; }
  mkdir -p "$HERE/SDL3"
  cp -a "$ARCH/src" "$ARCH/include" "$ARCH/cmake" "$ARCH/build-scripts" \
        "$ARCH/CMakeLists.txt" "$ARCH/LICENSE.txt" "$ARCH/README.md" \
        "$ARCH/WhatsNew.txt" "$ARCH/wayland-protocols" "$HERE/SDL3/"
  rm -rf "$ARCH"
  mark SDL3 https://github.com/libsdl-org/SDL.git "$SDL_TAG"
fi

echo "== готово =="
du -sh "$HERE"/NVRHI "$HERE"/Vulkan-Headers "$HERE"/DirectX-Headers "$HERE"/SDL3 2>/dev/null
