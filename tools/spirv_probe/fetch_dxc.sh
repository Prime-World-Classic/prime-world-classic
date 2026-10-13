#!/usr/bin/env bash
# fetch_dxc.sh — скачать DXC (Microsoft HLSL -> SPIR-V) в vendor/dxc.
# Не коммитить: бинарник ~100 МБ, нужен только для спайка/генерации.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VER="${DXC_VER:-v1.9.2609}"
URL="https://github.com/microsoft/DirectXShaderCompiler/releases/download/${VER}/linux_dxc_2026_09_28.x86_x64.tar.gz"
mkdir -p "$HERE/vendor"
cd "$HERE/vendor"
[ -x dxc/bin/dxc ] && { echo "уже есть: $(dxc/bin/dxc --version 2>/dev/null | head -1)"; exit 0; }
curl -sL -o dxc.tar.gz "$URL"
mkdir -p dxc && tar xzf dxc.tar.gz -C dxc && rm -f dxc.tar.gz
echo "DXC: $(LD_LIBRARY_PATH=$HERE/vendor/dxc/lib dxc/bin/dxc --version 2>&1 | head -1)"
