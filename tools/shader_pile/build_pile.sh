#!/usr/bin/env bash
# Собирает zzzresult.pile из перегенерированных .shd на tiny10 и ставит его в
# Packs стенда. Порядок такой: шейдеры компилируются только на настоящей Windows
# (см. tools/hlsl_compile_test/README.md), там же пакуется pile — FilePileCompiler
# из дерева это x86-консоль, на Win10 x64 запускается как есть.
#
#   1) генерация:        gen_shaders.sh
#   2) упаковка+установка: build_pile.sh
#   3) гейт:             E2E на tiny10 (скил pw-client)
set -euo pipefail

GUEST="${GUEST:-pw-tiny10}"
SRC="${SRC:-C:\\tools\\hlsl}"          # где лежат sc\ShaderCompiler.exe, sc\FilePileCompiler.exe, data\
PACKDIR="$SRC\\data"                   # packdir: в нём Shaders\UberShaders\*.shd
PILE="zzzresult"
PACKS='C:\pw64\Game\Packs'

ssh_pile() {
  timeout 300 ssh -o ConnectTimeout=5 "$GUEST" "$1" 2>&1 | grep -v "post-quantum\|store now\|decrypt later\|may need to be upgraded"
}

echo "== 1. упаковка ($PACKDIR -> $PILE.pile)"
ssh_pile "cd /d $SRC & del $PILE.pile 2>nul & sc\\FilePileCompiler.exe -output $PILE -packdir $PACKDIR -masks *.shd -verbose 1 > pile.log 2>&1 & type pile.log | findstr /C:\"Total\" /C:\"File\" /C:\"Error\" & dir $SRC\\$PILE.pile"

echo "== 2. установка в $PACKS (с бэкапом старого zzzresult.pile)"
ssh_pile "copy /Y $SRC\\$PILE.pile $PACKS\\$PILE.pile & dir $PACKS\\$PILE.pile"

echo
echo "Порядок намеренно zzz*: PileFileSystem::ScanForPiles() сортирует найденные"
echo "*.pile компаратором NStr::ICmp(...) > 0, т.е. по убыванию, и поиск по файлу"
echo "идёт по этому списку до первого совпадения -> zzzresult.pile перекрывает"
echo "и result.pile, и zpatch_fev.pile."
