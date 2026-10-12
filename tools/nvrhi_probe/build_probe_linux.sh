#!/usr/bin/env bash
# build_probe_linux.sh — нативная Linux-сборка пробы (g++), чтобы снять
# Vulkan-смоук на хосте с реальным GPU: домашняя машина — RTX 3060 +
# nvidia_icd, winevulkan же даёт только «драйвер есть, surface нет».
# Это НЕ путь клиента (клиент — Windows), это проверка Vulkan-бэкенда NVRHI.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build-linux"
[ "${1:-}" = "--clean" ] && rm -rf "$BUILD"
mkdir -p "$BUILD"
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
echo "== артефакт: $BUILD/nvrhi_probe"
if [ "${1:-}" = "--run" ] || [ "${2:-}" = "--run" ]; then
  echo "== запуск:"; "$BUILD/nvrhi_probe"
fi
