#!/usr/bin/env bash
# build_and_run.sh — собрать и прогнать смоук писателей снимка кадра.
#
# Render/screenshot.cpp после выноса EBackendPixelLayout в
# renderpixellayout.h не зависит от D3D9, Windows и stdafx клиента — поэтому
# тест нативный (g++ + zlib), без Wine и без графа клиента.
#
#   ./build_and_run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
R1117="$(cd "$HERE/../../pw/branches/r1117" && pwd)"
OUT="$HERE/out"
mkdir -p "$OUT"

CXX="${CXX:-g++}"
"$CXX" -std=c++11 -O1 -Wall -Wextra \
    -I "$R1117/Src/Render" -I "$R1117/Vendor/zlib/include" \
    "$HERE/main.cpp" "$R1117/Src/Render/screenshot.cpp" -o "$OUT/smoke" -lz

cd "$OUT"
./smoke
python3 "$HERE/check.py"
