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
#   1. Wine-префикс: автопоиск префикса с VS2008 SP1 + Windows SDK v6.0A
#   2. Src/PW_Game/server_ip.h — gitignored (per-deploy); если его нет,
#      генерируется из Tools/WineClientBuild/server_ip.h.template
#   3. model.json — разбор vcproj (Tools/WineClientBuild/parse_vcproj.py)
#   4. Компиляция + линковка (Tools/WineClientBuild/build_client.py)
#   5. Runtime-файлы из .refs (аналог post-build CopyReference.exe)
#   6. (опционально) копирование результата в pw_publish/branch/Client/PvP/Bin
#
# Использование (из чистого клона — этого достаточно):
#   ./build_client_wine.sh                       # полная сборка
#   ./build_client_wine.sh --prefix=/path/prefix # другой Wine-префикс
#   ./build_client_wine.sh --only=PW_Client      # один проект (без линковки)
#   ./build_client_wine.sh --dryrun              # только план jobs
#   ./build_client_wine.sh --keep-going          # не останавливаться на первой ошибке
#   ./build_client_wine.sh --no-refs             # не копировать .refs
#   ./build_client_wine.sh --deploy              # копировать в pw_publish/.../Bin
#   ./build_client_wine.sh --clean               # очистить выходной каталог
#   ./build_client_wine.sh --regen-ip            # пересоздать server_ip.h из шаблона
#   PW_BUILD_JOBS=8 ./build_client_wine.sh       # параллельность
#
# Значения для server_ip.h (по умолчанию — локальный стенд 127.0.0.1):
#   PW_SERVER_IP PW_SESSION_TOKEN PW_API_KEY PW_BACKEND_HTTP_PORT
#   PW_SERVER_PORT PW_LOGIN_PORT PW_CLUSTER_PORT_FRONT PW_CLUSTER_PORT_BACK
#   PW_SYNCHRONIZER_PORT
#   либо файл Tools/WineClientBuild/server_ip.env (gitignored, KEY=VALUE).
#   Существующий server_ip.h не перезаписывается (per-deploy значения).
#
# Диагностические флаги компилятора (требуют --clean — uptodate-логика их не видит):
#   PW_EXTRA_DEFS='MAX_STACK_SIZE=10;NI_DUMP_LEAKS_TO_FILE' — реестр аллокаций
#   PW_EXTRA_OPTS='/Oy-' — кадровые указатели (иначе в WOW64 стеки не собираются)
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
REGEN_IP=0
for arg in "$@"; do
    case "$arg" in
        --prefix=*)   WINEPREFIX="${arg#--prefix=}"; PREFIX_FORCED=1 ;;
        --config=*)   CFG="${arg#--config=}"; OUTDIR="$BR/Src/_${CFG%%|*}" ;;
        --only=*|--dryrun) EXTRA+=("$arg") ;;
        --keep-going) EXTRA+=("--keep-going"); export PW_KEEP_GOING=1 ;;
        --no-refs)    DO_REFS=0 ;;
        --deploy)     DO_DEPLOY=1 ;;
        --clean)      DO_CLEAN=1 ;;
        --regen-ip)   REGEN_IP=1 ;;
        -h|--help)    grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -50; exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done
PREFIX_FORCED="${PREFIX_FORCED:-0}"

# ---------------------------------------------------- 0. Wine-префикс (автопоиск)
# Критерий пригодности: cl.exe из VS2008 + Include/Lib Windows SDK v6.0A.
prefix_ok() {
    local p="$1"
    [ -d "$p" ] || return 1
    [ -x "$p/drive_c/Program Files (x86)/Microsoft Visual Studio 9.0/VC/bin/cl.exe" ] || return 1
    [ -d "$p/drive_c/Program Files/Microsoft SDKs/Windows/v6.0A/Include" ] || return 1
    [ -d "$p/drive_c/Program Files/Microsoft SDKs/Windows/v6.0A/Lib" ] || return 1
    return 0
}

if [ "$PREFIX_FORCED" != 1 ] && ! prefix_ok "$WINEPREFIX"; then
    for cand in "$HOME/pwbuild/wine32" "$HOME/.wine-vs2008" "$HOME/.wine" \
                $(ls -d "$HOME"/*/wine* "$HOME"/.wine* 2>/dev/null); do
        if prefix_ok "$cand"; then WINEPREFIX="$cand"; break; fi
    done
fi
if ! prefix_ok "$WINEPREFIX"; then
    echo "ERROR: не найден Wine-префикс с VS2008 SP1 + Windows SDK v6.0A." >&2
    echo "Проверены: $WINEPREFIX и каталоги в \$HOME (*/wine*, .wine*)." >&2
    echo "Создание: WINEARCH=win32 WINEPREFIX=~/.wine-vs2008 winecfg, затем установить" >&2
    echo "VS2008 SP1 (devenv не нужен) + SDK v6.0A — см. BUILD_CLIENT_WINE.md." >&2
    exit 1
fi
echo "== Wine-префикс: $WINEPREFIX"
# обязательно экспортировать: build_client.py сам запускает wine и берёт
# WINEPREFIX из окружения (его дефолт ~/.wine-vs2008), иначе wine создаёт
# пустой префикс, а все cl-вызовы падают с «cl не является программой»
export WINEPREFIX

VC_WIN='C:\Program Files (x86)\Microsoft Visual Studio 9.0'
SDK_WIN='C:\Program Files\Microsoft SDKs\Windows\v6.0A'
VC="$WINEPREFIX/drive_c/Program Files (x86)/Microsoft Visual Studio 9.0"
SDK="$WINEPREFIX/drive_c/Program Files/Microsoft SDKs/Windows/v6.0A"
echo "== компилятор:"
WINEPREFIX="$WINEPREFIX" WINEDEBUG=-all wine cmd /c "cl" 2>&1 | sed -n '1p'

# ---------------------------------------------------- 0b. server_ip.h (gitignored, per-deploy)
IPH="$BR/Src/PW_Game/server_ip.h"
IPENV="$TOOLS/server_ip.env"
if [ -f "$IPENV" ]; then
    echo "== значения сервера: $(basename "$IPENV")"
    set -a; . "$IPENV"; set +a
fi
: "${PW_SERVER_IP:=127.0.0.1}"
: "${PW_SESSION_TOKEN:=Tester00Tester00Tester00Tester00}"
: "${PW_API_KEY:=changeme}"
: "${PW_BACKEND_HTTP_PORT:=7777}"
: "${PW_SERVER_PORT:=27300}"
: "${PW_LOGIN_PORT:=27301}"
: "${PW_CLUSTER_PORT_FRONT:=27310}"
: "${PW_CLUSTER_PORT_BACK:=27340}"
: "${PW_SYNCHRONIZER_PORT:=27302}"
[ "$REGEN_IP" = 1 ] && rm -f "$IPH"
if [ ! -f "$IPH" ]; then
    echo "== генерация Src/PW_Game/server_ip.h: SERVER_IP=$PW_SERVER_IP PORT=$PW_SERVER_PORT (из шаблона)"
    sed -e "s|@SERVER_IP@|$PW_SERVER_IP|g" \
        -e "s|@SESSION_TOKEN@|$PW_SESSION_TOKEN|g" \
        -e "s|@API_KEY@|$PW_API_KEY|g" \
        -e "s|@BACKEND_HTTP_PORT@|$PW_BACKEND_HTTP_PORT|g" \
        -e "s|@SERVER_PORT@|$PW_SERVER_PORT|g" \
        -e "s|@LOGIN_PORT@|$PW_LOGIN_PORT|g" \
        -e "s|@CLUSTER_PORT_FRONT@|$PW_CLUSTER_PORT_FRONT|g" \
        -e "s|@CLUSTER_PORT_BACK@|$PW_CLUSTER_PORT_BACK|g" \
        -e "s|@SYNCHRONIZER_PORT@|$PW_SYNCHRONIZER_PORT|g" \
        "$TOOLS/server_ip.h.template" > "$IPH"
elif [ "$REGEN_IP" != 1 ]; then
    echo "== server_ip.h уже есть — не перезаписываю (--regen-ip — пересоздать из шаблона)"
fi

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
