#!/usr/bin/env bash
# =============================================================================
# build_client_wine.sh — сборка клиента Prime World Classic (PW_Game.exe,
# VS2008 SP1, x86) под Wine из корня репозитория prime-world-classic.
#
# Почему не devenv/MSBuild: VS2008 требует .NET Framework, которого в Wine нет
# (devenv.com -> "Cannot find one or more components"). Поэтому сборку выполняет
# Tools/WineClientBuild/build_client.py: он читает vcproj и вызывает cl/lib/rc/link/mt
# напрямую, воспроизводя флаги конфигурации ShippingSingleExe|Win32.
#
# Этапы:
#   1. Проверка Wine-префикса (32-битный, VS2008 SP1 + Windows SDK v6.0A)
#   2. model.json — разбор vcproj (Tools/WineClientBuild/parse_vcproj.py)
#   3. Компиляция + линковка (Tools/WineClientBuild/build_client.py)
#   4. Runtime-файлы из .refs (аналог post-build CopyReference.exe)
#   5. (опционально) копирование результата в pw_publish/branch/Client/PvP/Bin
#
# Использование:
#   ./build_client_wine.sh                       # полная сборка
#   ./build_client_wine.sh --prefix=/path/prefix # другой Wine-префикс
#   ./build_client_wine.sh --only=PW_Client      # один проект (без линковки)
#   ./build_client_wine.sh --dryrun              # только план jobs
#   ./build_client_wine.sh --keep-going          # не останавливаться на первой ошибке
#   ./build_client_wine.sh --no-refs             # не копировать .refs
#   ./build_client_wine.sh --deploy              # копировать в pw_publish/.../Bin
#   ./build_client_wine.sh --clean               # очистить выходной каталог
#   PW_BUILD_JOBS=8 ./build_client_wine.sh       # параллельность
#
# Требования: Wine (32-битный префикс) + VS2008 SP1 + Windows SDK v6.0A в префиксе.
#   Установка: WINEARCH=win32 WINEPREFIX=~/.wine-vs2008 winecfg, затем установить
#   VS2008 SP1 (devenv не нужен, достаточно VC + SDK) — см. BUILD_CLIENT_WINE.md.
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
BR="$REPO/pw/branches/r1117"
TOOLS="$BR/Tools/WineClientBuild"
CFG="${PW_CFG:-ShippingSingleExe|Win32}"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs2008}"
OUTDIR="$BR/Src/_${CFG%%|*}"
JOBS="${PW_BUILD_JOBS:-8}"
RUNTIME="$TOOLS/runtime"

EXTRA=()
DO_REFS=1
DO_DEPLOY=0
DO_CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --prefix=*)   WINEPREFIX="${arg#--prefix=}" ;;
        --config=*)   CFG="${arg#--config=}"; OUTDIR="$BR/Src/_${CFG%%|*}" ;;
        --only=*|--dryrun) EXTRA+=("$arg") ;;
        --keep-going) EXTRA+=("--keep-going"); export PW_KEEP_GOING=1 ;;
        --no-refs)    DO_REFS=0 ;;
        --deploy)     DO_DEPLOY=1 ;;
        --clean)      DO_CLEAN=1 ;;
        -h|--help)    grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -40; exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done

echo "== Wine-префикс: $WINEPREFIX"
if [ ! -d "$WINEPREFIX" ]; then
    echo "ERROR: префикс не найден: $WINEPREFIX" >&2
    echo "Создайте: WARCH=win32 WINEPREFIX=$WINEPREFIX winecfg и установите VS2008 SP1 + SDK 6.0A" >&2
    exit 1
fi

VC_WIN='C:\Program Files (x86)\Microsoft Visual Studio 9.0'
SDK_WIN='C:\Program Files\Microsoft SDKs\Windows\v6.0A'
VC="$WINEPREFIX/drive_c/Program Files (x86)/Microsoft Visual Studio 9.0"
SDK="$WINEPREFIX/drive_c/Program Files/Microsoft SDKs/Windows/v6.0A"
if [ ! -x "$VC/VC/bin/cl.exe" ]; then
    echo "ERROR: в префиксе нет VS2008 (cl.exe): $VC/VC/bin" >&2
    exit 1
fi
if [ ! -d "$SDK/Include" ] || [ ! -d "$SDK/Lib" ]; then
    echo "ERROR: в префиксе нет Windows SDK v6.0A (Include/Lib): $SDK" >&2
    exit 1
fi
echo "== компилятор:"
WINEPREFIX="$WINEPREFIX" WINEDEBUG=-all wine cmd /c "cl" 2>&1 | sed -n '1p'

if [ "$DO_CLEAN" = 1 ]; then
    echo "== очистка: $OUTDIR"
    rm -rf "$OUTDIR"
fi
mkdir -p "$OUTDIR" "$RUNTIME"

# ---------------------------------------------------- 1. модель проектов (vcproj)
export PW_SRC="$BR/Src" PW_CFG="$CFG" PW_LOGDIR="$RUNTIME" PW_MODEL="$RUNTIME/model.json"
echo "== разбор vcproj ($CFG)"
python3 "$TOOLS/parse_vcproj.py" "$BR/Src" "$PW_MODEL" > "$RUNTIME/parse.out"
echo "   проектов: $(python3 -c "import json;print(len(json.load(open('$PW_MODEL'))))")"

# ---------------------------------------------------- 2. компиляция и линковка
echo "== сборка ($JOBS параллельно)"
python3 "$TOOLS/build_client.py" ${EXTRA[@]+"${EXTRA[@]}"}

if [[ " ${EXTRA[*]-} " == *" --dryrun "* ]]; then
    exit 0
fi

# ---------------------------------------------------- 3. runtime-файлы (.refs)
if [ "$DO_REFS" = 1 ]; then
    echo "== runtime-файлы из .refs"
    python3 "$TOOLS/refs_copy.py" "$BR/Src" "$OUTDIR" > "$RUNTIME/refs.out"
    tail -1 "$RUNTIME/refs.out"
fi

# ---------------------------------------------------- 4. deploy
if [ "$DO_DEPLOY" = 1 ]; then
    BIN="$REPO/pw_publish/branch/Client/PvP/Bin"
    mkdir -p "$BIN"
    cp -a "$OUTDIR/PW_Game.exe" "$BIN/"
    [ -f "$OUTDIR/PW_Game.exe.manifest" ] && cp -a "$OUTDIR/PW_Game.exe.manifest" "$BIN/"
    echo "== скопировано в $BIN"
fi

echo
echo "PW_Game.exe: $OUTDIR/PW_Game.exe ($(stat -c %s "$OUTDIR/PW_Game.exe" 2>/dev/null || echo missing) байт)"
