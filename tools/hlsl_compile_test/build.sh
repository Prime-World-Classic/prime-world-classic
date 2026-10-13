#!/usr/bin/env bash
# Сборка hlsl_compile_test под Wine тем же тулчейном, что и клиент
# (cl 14.44 x64, /MT — чтобы exe был самодостаточным и запускался на tiny10).
# Обёртка-cmd строится из сгенерированной x64-wine.cmd: берём её шапку
# (PATH/INCLUDE/LIB) и подставляем свою команду cl.
set -euo pipefail
cd "$(dirname "$0")"

BR="$PWD/../../pw/branches/r1117"
CCT="$BR/Tools/ClientCMake"
SRC_CMD="$CCT/x64-wine.cmd"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
[ -f "$SRC_CMD" ] || { echo "нет $SRC_CMD — сначала ./build_client_x64_wine.sh --gen-only" >&2; exit 1; }

mkdir -p build
OUT="$(pwd -P)"
WOUT="Z:${OUT}"                       # wine cmd требует букву диска

{
  head -5 "$SRC_CMD"                       # @echo off, PATH, INCLUDE, LIB, WINEDEBUG
  echo "cd /d ${WOUT//\//\\}"
  echo "cl /nologo /W3 /MT /EHsc /Fe:build\\hlsl_compile_test.exe hlsl_compile_test.cpp > build\\cc.log 2>&1"
  echo "if errorlevel 1 type build\\cc.log & exit /b 1"
  echo "echo BUILD OK"
} > _build_tmp.cmd

WINEDEBUG=-all WINEPREFIX="$WINEPREFIX" wine cmd /c "${WOUT//\//\\}\\_build_tmp.cmd"
rm -f _build_tmp.cmd
ls -la build/hlsl_compile_test.exe
