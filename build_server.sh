#!/usr/bin/env bash
# =============================================================================
# build_server.sh — полная сборка сервера UniServerApp (Prime World Classic)
# на Linux из корня репозитория prime-world-classic.
#
# Этапы:
#   1. Проверка/установка системных зависимостей
#   2. Сборка vendor-библиотек:
#        ACE 5.7   -> Vendor/ACE_wrappers/lib/libACE-5.7.so (+ libACE.so)
#        Terabit   -> Vendor/Terabit/lib/TProactor.so, IOTerabit.so
#        zdll.so   -> ссылки на системный zlib там, где их ждут компоненты
#   3. Генерация CMake-проектов через TestFramework (Python 2.7, conda):
#        UniServerApp.auto/UniServerApp/CMakeLists.txt
#   4. Сборка UniServerApp (cmake + make)
#   5. Копирование бинарника в pw_publish/branch/Server/PvX/Bin
#
# Использование:
#   ./build_server.sh                     # полная сборка
#   ./build_server.sh --vendor-only       # только vendor-библиотеки
#   ./build_server.sh --no-vendor         # пропустить vendor (уже собран)
#   ./build_server.sh --rebuild-vendor    # пересобрать vendor принудительно
#   ./build_server.sh --no-deploy         # не копировать бинарник в pw_publish
#   ./build_server.sh --shipping          # shipping-сборка: release-конфигурация
#                                         # генератора, добавляет -D_SHIPPING
#
# Исправления, обеспечивающие рабочую сборку (см. BUILD_LINUX.md):
#   * абсолютные пути: генератор запущен из каталога UniServer, а путь к
#     SPIPE_Addr.cpp в Tools/TestFramework/main.py резолвится динамически
#     (через TestFrameworkPath), а не захардкожен; UniServerApp.auto
#     пересоздаётся каждый запуск, кэш cmake сбрасывается при смене пути;
#   * конфигурация библиотек: vendor-библиотеки кладутся в точные каталоги,
#     ожидаемые сгенерированным CMake (libACE.so, TProactor.so, IOTerabit.so,
#     zdll.so), создаются server_ip.h (gitignored per-deploy файл);
#   * повреждённые токены 'fidone' в Vendor/ACE_wrappers/configure чинятся
#     до запуска configure; каталог сборки ACE всегда чистый.
#
# Требования: Ubuntu/Debian x86_64. Python 2.7 ставится автоматически,
# если не найден: Miniconda скачивается в ~/miniconda3, создаётся
# окружение py27 (нужна сеть).
# =============================================================================
set -euo pipefail

# ------------------------------------------------------------------ аргументы
VENDOR_ONLY=0
NO_VENDOR=0
REBUILD_VENDOR=0
NO_DEPLOY=0
SHIPPING=0
for arg in "$@"; do
    case "$arg" in
        --vendor-only)    VENDOR_ONLY=1 ;;
        --no-vendor)      NO_VENDOR=1 ;;
        --rebuild-vendor) REBUILD_VENDOR=1 ;;
        --no-deploy)      NO_DEPLOY=1 ;;
        --shipping)       SHIPPING=1 ;;
        -h|--help)
            grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -30
            exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done

log()  { echo -e "\n\033[1;32m==> $* ==\033[0m"; }
warn() { echo -e "\033[1;33mWARN: $*\033[0m"; }
die()  { echo -e "\033[1;31mERROR: $*\033[0m" >&2; exit 1; }

# ------------------------------------------------------------------- пути
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BR=$ROOT/pw/branches/r1117
ACE=$BR/Vendor/ACE_wrappers
TB=$BR/Vendor/Terabit
WS=$BR/Vendor/wsdlpull
UNI=$BR/Src/Game/PF/UniServer
TF=$BR/Tools/TestFramework
SERVER_IP_H=$BR/Src/PW_Game/server_ip.h
BUILD_DIR=$UNI/build_linux
DEPLOY_DIR=$ROOT/pw_publish/branch/Server/PvX/Bin

[ -d "$BR" ] || die "не найден $BR — скрипт должен лежать в корне prime-world-classic"

# -------------------------------------------- 1. системные зависимости
log "1/5. Системные зависимости"
pkgs=""
command -v g++      >/dev/null || pkgs="$pkgs build-essential"
command -v cmake    >/dev/null || pkgs="$pkgs cmake"
command -v make     >/dev/null || pkgs="$pkgs make"
command -v automake >/dev/null || pkgs="$pkgs automake"
command -v aclocal  >/dev/null || pkgs="$pkgs autoconf"
command -v thrift   >/dev/null || pkgs="$pkgs thrift-compiler"
ls /usr/share/libtool*/build-aux/ltmain.sh >/dev/null 2>&1 || pkgs="$pkgs libtool"
[ -e /usr/include/zlib.h ]             || pkgs="$pkgs zlib1g-dev"
[ -e /usr/include/openssl/ssl.h ]      || pkgs="$pkgs libssl-dev"
{ [ -e /usr/include/curl/curl.h ] || [ -e /usr/include/x86_64-linux-gnu/curl/curl.h ]; } \
    || pkgs="$pkgs libcurl4-openssl-dev"

if [ -n "${pkgs// /}" ]; then
    if command -v apt-get >/dev/null; then
        SUDO=""; [ "$(id -u)" != "0" ] && SUDO="sudo"
        echo "Устанавливаю:$pkgs"
        # shellcheck disable=SC2086
        $SUDO apt-get update -qq
        # shellcheck disable=SC2086
        $SUDO apt-get install -y $pkgs
    else
        die "Нет зависимостей:$pkgs (apt-get не найден — установите вручную)"
    fi
else
    echo "Все зависимости на месте"
fi

# ------------------------------------------- 2. vendor: ACE 5.7
log "2/5. Vendor-библиотеки: ACE 5.7"
if [ "$REBUILD_VENDOR" = 1 ] && [ -f "$ACE/lib/libACE-5.7.so" ]; then
    rm -f "$ACE/lib/libACE-5.7.so" "$ACE/lib/libACE.so"
fi
if [ -f "$ACE/lib/libACE-5.7.so" ]; then
    echo "libACE-5.7.so уже на месте (пропуск; --rebuild-vendor для пересборки)"
elif [ "$NO_VENDOR" = 1 ]; then
    die "libACE-5.7.so не найден, а задан --no-vendor — запустите без этого флага"
else
    # 2.1. Чиним configure: в исходниках ACE 5.7 встречаются повреждённые
    #      токены 'fidone' (обрезка 'fi' + 'done') — configure падает с
    #      "unexpected end of file from `for' command". Чиним sed'ом.
    if grep -q '^fidone$' "$ACE/configure"; then
        n=$(grep -c '^fidone$' "$ACE/configure")
        sed -i 's/^fidone$/fi; done/' "$ACE/configure"
        echo "configure: исправлено повреждённых токенов: $n"
    fi

    # 2.2. Чистый каталог сборки (старые Makefile/config.status ломают
    #      повторный configure при VPATH-сборке)
    rm -rf "$ACE/build"
    mkdir -p "$ACE/build"

    # 2.3. Autotools aux-файлы (ltmain.sh, compile, config.guess, ...)
    if [ ! -f "$ACE/aux_config/ltmain.sh" ] || [ ! -f "$ACE/aux_config/compile" ]; then
        mkdir -p "$ACE/aux_config"
        AUXSRC=""
        for d in /usr/share/libtool/build-aux /usr/share/libtool-2/build-aux \
                 /usr/share/automake-1.18 /usr/share/automake-1.17 /usr/share/automake-1.16 \
                 /usr/share/autoconf/build-aux /usr/share/misc; do
            if [ -f "$d/ltmain.sh" ]; then AUXSRC=$d; break; fi
        done
        [ -n "$AUXSRC" ] || die "не найден ltmain.sh — установите пакет libtool"
        for f in compile config.guess config.sub depcomp install-sh ltmain.sh missing; do
            [ -f "$AUXSRC/$f" ] && cp -f "$AUXSRC/$f" "$ACE/aux_config/$f"
        done
        for d in /usr/share/automake-1.18 /usr/share/automake-1.17 /usr/share/automake-1.16; do
            if [ -f "$d/test-driver" ]; then cp -f "$d/test-driver" "$ACE/aux_config/test-driver"; break; fi
        done
        chmod +x "$ACE/aux_config/"*
        echo "aux_config создан из: $AUXSRC"
    fi

    # 2.4. Makefile.in (генерируются из Makefile.am, если отсутствуют)
    if [ ! -f "$ACE/ace/Makefile.in" ]; then
        ( cd "$ACE" && aclocal )
        ( cd "$ACE" && find . -name Makefile.am -not -path './build/*' -print0 \
            | xargs -0 -n1 dirname | sort -u \
            | while IFS= read -r d; do
                  ( cd "$ACE/$d" && automake --add-missing --foreign ) \
                      || warn "$d: automake завершился с ошибкой (Makefile.in мог быть создан)"
              done )
    fi

    # 2.4b. Запрет ложной регенерации configure. После git clone mtime у
    #      m4/*.m4 / configure.ac может оказаться строго новее, чем у
    #      configure (особенно на exFAT/NTFS, где точность меток — секунда),
    #      и make пересоберёт configure системным autoconf. Свежий autoconf
    #      (2.7x) для configure.ac 2009 г. выдаёт битый скрипт: незакрытый
    #      `for' → "syntax error: unexpected end of file from `for' command".
    #      touch делает configure новее всех зависимостей — make не тронет его.
    touch "$ACE/configure"

    # 2.5. configure + make. UniServerApp линкует только главную libACE,
    #      поэтому SUBDIRS обрезаем: ace/SSL не компилируется под OpenSSL>=1.1,
    #      ETCL/Monitor_Control/ACEXML/netsvcs не нужны.
    ( cd "$ACE/build" && ../configure --disable-tests --disable-samples )
    trim_subdirs() {  # $1 = Makefile, $2 = значение SUBDIRS
        awk -v val="$2" '
            /^SUBDIRS = / {
                if ($0 ~ /\\$/) { print "SUBDIRS = " val; inskip = 1 }
                else print
                next
            }
            inskip { if ($0 ~ /\\$/) next; else inskip = 0; next }
            { print }
        ' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
    }
    trim_subdirs "$ACE/build/Makefile" ace
    trim_subdirs "$ACE/build/ace/Makefile" .
    ( cd "$ACE/build" && make -j"$(nproc)" )

    ACE_LIB="$ACE/build/ace/.libs/libACE-5.7.so"
    [ -f "$ACE_LIB" ] || ACE_LIB="$ACE/build/lib/libACE-5.7.so"
    [ -f "$ACE_LIB" ] || die "не найден результат сборки ACE (ace/.libs/libACE-5.7.so)"
    cp -f "$ACE_LIB" "$ACE/lib/libACE-5.7.so"
fi
ln -sf libACE-5.7.so "$ACE/lib/libACE.so"
echo "libACE-5.7.so / libACE.so -> Vendor/ACE_wrappers/lib/"

# ------------------------------------------- 2b. vendor: Terabit
if [ "$REBUILD_VENDOR" = 1 ] && [ -f "$TB/lib/IOTerabit.so" ]; then
    rm -f "$TB/lib/IOTerabit.so" "$TB/lib/TProactor.so"
fi
if [ -f "$TB/lib/IOTerabit.so" ] && [ -f "$TB/lib/TProactor.so" ]; then
    echo "TProactor.so / IOTerabit.so уже на месте (пропуск)"
elif [ "$NO_VENDOR" = 1 ]; then
    die "TProactor.so / IOTerabit.so не найдены, а задан --no-vendor — запустите без этого флага"
else
    [ -f "$ACE/lib/libACE-5.7.so" ] || die "нужна libACE-5.7.so для сборки Terabit"
    rm -rf "$TB/build"
    cmake -S "$TB" -B "$TB/build"
    cmake --build "$TB/build" -j"$(nproc)"
    cp -f "$TB/build/libTProactor.so" "$TB/lib/TProactor.so"
    cp -f "$TB/build/libIOTerabit.so" "$TB/lib/IOTerabit.so"
fi
# Имена, которые ждут линковщик UniServerApp (компоненты Terabit + wsdlpull)
ln -sf TProactor.so "$TB/lib/libTProactor.so"
ln -sf IOTerabit.so "$TB/lib/libIOTerabit.so"
ZLIB_SO=$(ls /usr/lib/x86_64-linux-gnu/libz.so /lib/x86_64-linux-gnu/libz.so /usr/lib/libz.so 2>/dev/null | head -1 || true)
[ -n "$ZLIB_SO" ] || die "не найден системный libz.so"
ln -sf "$ZLIB_SO" "$TB/lib/libzdll.so"
mkdir -p "$WS/lib/Debug" "$WS/lib/Release"
ln -sf "$ZLIB_SO" "$WS/lib/Debug/zdll.so"
ln -sf "$ZLIB_SO" "$WS/lib/Release/zdll.so"
echo "TProactor.so / IOTerabit.so / libzdll.so / zdll.so на месте"

if [ "$VENDOR_ONLY" = 1 ]; then
    log "Готово (vendor-only)"
    exit 0
fi

# ------------------------------------------- 2c. server_ip.h (gitignored)
# Per-deploy файл конфигурации (пример — в README.md). Не переопределяем,
# если пользователь уже создал его сам.
if [ ! -f "$SERVER_IP_H" ]; then
    cat > "$SERVER_IP_H" <<'SERVER_IP_EOF'
#pragma once

// =============================================================================
// server_ip.h — серверная IP/порт конфигурация (per-deploy файл, gitignored).
//
// Не версионируется в репозитории (см. .gitignore). Создаётся при сборке
// (build_server.sh) со значениями для локального запуска; перед развёртыванием
// замените адреса/ключи на фактические (пример — в README.md).
// =============================================================================

#define SESSION_TOKEN "Tester00Tester00Tester00Tester00"
#define API_KEY "APIKEY00APIKEY00APIKEY00APIKEY00APIKEY00"

// Main server IP
#define SERVER_IP_W L"127.0.0.1"
#define SERVER_IP "127.0.0.1"

// Proxy server IP
#define SERVER_PROXY_IP_W L"127.0.0.1"
#define SERVER_PROXY_IP "127.0.0.1"

// Radmin server IP
#define MIRROR_SERVER_IP_W L"127.0.0.1"
#define MIRROR_SERVER_IP "127.0.0.1"

#define SERVER_PORT "27300"
#define LOGIN_PORT "27301"

#define SERVER_CLUSTER_PORT_FRONT 27310
#define SERVER_CLUSTER_PORT_BACK 27340

#define SYNCHRONIZER_PORT 27302

extern int usedServer;
static const char* SERVER_IP_ARRAY[] = {SERVER_IP, MIRROR_SERVER_IP, SERVER_PROXY_IP};
static const wchar_t* SERVER_IP_W_ARRAY[] = {SERVER_IP_W, MIRROR_SERVER_IP_W, SERVER_PROXY_IP_W};
SERVER_IP_EOF
    echo "создан $SERVER_IP_H (локальные значения; см. пример в README.md)"
else
    echo "server_ip.h уже существует — не трогаем"
fi

# ------------------------------------------- 3. генерация CMake (py2.7)
log "3/5. Генерация CMake (TestFramework, Python 2.7)"

find_py2() {
    local c
    for c in "$HOME/miniconda3/envs/py27/bin/python" \
             "$HOME/anaconda3/envs/py27/bin/python" \
             /opt/conda/envs/py27/bin/python; do
        [ -x "$c" ] && { echo "$c"; return 0; }
    done
    local conda_bin base
    conda_bin=$(command -v conda || true)
    if [ -n "$conda_bin" ]; then
        base=$("$conda_bin" info --base 2>/dev/null | tail -1 || true)
        if [ -n "$base" ] && [ -x "$base/envs/py27/bin/python" ]; then
            echo "$base/envs/py27/bin/python"
            return 0
        fi
    fi
    command -v python2 >/dev/null && { command -v python2; return 0; }
    return 1
}

PY2=$(find_py2 || true)
if [ -z "$PY2" ]; then
    # conda может быть установлен, но не активирован в этом shell —
    # смотрим в стандартных местах, кроме PATH
    CONDA_BIN=$(command -v conda || true)
    for cbin in "$HOME/miniconda3/bin/conda" "$HOME/anaconda3/bin/conda" \
                "$HOME/mambaforge/bin/conda" /opt/conda/bin/conda; do
        [ -z "$CONDA_BIN" ] && [ -x "$cbin" ] && CONDA_BIN=$cbin
    done

    # conda нет вообще — устанавливаем Miniconda (нужна сеть)
    if [ -z "$CONDA_BIN" ]; then
        if [ -d "$HOME/miniconda3" ] && [ ! -x "$HOME/miniconda3/bin/conda" ]; then
            die "~/miniconda3 существует, но в нём нет bin/conda — исправьте установку или удалите каталог, затем повторите"
        fi
        CONDA_DL=""
        if command -v curl >/dev/null; then
            CONDA_DL="curl -fL -o /tmp/miniconda.sh"
        elif command -v wget >/dev/null; then
            CONDA_DL="wget -q -O /tmp/miniconda.sh"
        else
            die "не найдены curl и wget — установите curl (apt install curl) и повторите"
        fi
        case "$(uname -m)" in
            x86_64)          CONDA_ARCH=x86_64 ;;
            aarch64|arm64)   CONDA_ARCH=aarch64 ;;
            *) die "неподдерживаемая архитектура: $(uname -m)" ;;
        esac
        warn "conda не найден — устанавливаю Miniconda в ~/miniconda3 (нужна сеть)"
        # shellcheck disable=SC2086
        $CONDA_DL "https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-${CONDA_ARCH}.sh" \
            || die "не удалось скачать Miniconda"
        bash /tmp/miniconda.sh -b -p "$HOME/miniconda3" \
            || die "не удалось установить Miniconda"
        rm -f /tmp/miniconda.sh
        CONDA_BIN="$HOME/miniconda3/bin/conda"
    fi

    warn "Python 2.7 не найден — создаю conda-окружение py27 (нужны сеть и время)"
    # Только conda-forge с --override-channels: в свежих conda дефолтные
    # каналы Anaconda (pkgs/main, pkgs/r) требуют интерактивного принятия
    # ToS, а python 2.7 в conda-forge доступен без этого.
    "$CONDA_BIN" create -n py27 --override-channels -c conda-forge python=2.7 -y \
        || die "не удалось создать окружение py27 (python 2.7 из conda-forge)"
    PY2=$(find_py2 || true)
    if [ -z "$PY2" ]; then
        # окружение создано, но conda лежит в нестандартном месте
        CB_BASE=$("$CONDA_BIN" info --base 2>/dev/null | tail -1 || true)
        if [ -n "$CB_BASE" ] && [ -x "$CB_BASE/envs/py27/bin/python" ]; then
            PY2="$CB_BASE/envs/py27/bin/python"
        fi
    fi
    [ -n "$PY2" ] || die "окружение py27 создано, но python в нём не найден"
fi
[ -n "$PY2" ] || die "не найден Python 2.7. Установите: conda create -n py27 python=2.7 (conda-forge)"
"$PY2" -c 'import sys; sys.exit(0 if sys.version_info[0] == 2 else 1)' \
    || die "$PY2 не является Python 2: $("$PY2" --version 2>&1)"
echo "Python 2.7: $PY2"

# ВАЖНО (абсолютные пути): генератор резолвит относительные пути компонентов
# от каталога запуска — запуск строго из каталога UniServer. UniServerApp.auto
# пересоздаём, если его нет, если в нём остались абсолютные пути от прошлого
# расположения репозитория, либо если входные .application/.component
# файлы изменились. При неизменных входе и путях генерацию пропускаем,
# чтобы не заставлять make пересобирать всё с нуля при каждом запуске.
GEN_CMK=$UNI/UniServerApp.auto/UniServerApp/CMakeLists.txt
SPIPE_PATH=$ACE/ace/SPIPE_Addr.cpp
# Конфигурация генератора TestFramework:
#   debug (по умолчанию, прежнее поведение) — без _SHIPPING, с -g;
#   --shipping → release — добавляет -D_SHIPPING (выключает debug-логи
#   и dev-пути в игровой логике, см. UniServerApp.application).
if [ "$SHIPPING" = 1 ]; then
    GEN_CONF_ARGS="--configuration release"
else
    GEN_CONF_ARGS=""
fi
done_generating=1
if [ -f "$GEN_CMK" ] && \
   grep -q "ADD_LIBRARY(spip_e_addr_obj OBJECT $SPIPE_PATH)" "$GEN_CMK"; then
    # CMakeLists должен быть сгенерирован именно под нашу конфигурацию,
    # иначе переключение debug/shipping даст старую генерацию
    cfg_ok=1
    if [ "$SHIPPING" = 1 ]; then
        grep -q "_SHIPPING" "$GEN_CMK" || cfg_ok=0
    else
        grep -q "_SHIPPING" "$GEN_CMK" && cfg_ok=0
    fi
    STALE_CMPS=0
    if [ "$cfg_ok" = 0 ]; then
        STALE_CMPS=1
    else
        STALE_CMPS=$(find "$BR/Src" "$BR/Vendor" \( -name "*.application" -o -name "*.component" \) -newer "$GEN_CMK" 2>/dev/null | wc -l) || STALE_CMPS=1
    fi
    # find может завершиться с ошибкой на нечитаемых каталогах — считаем,
    # что входные файлы изменились, и пересоздаём UniServerApp.auto
    if [ "$STALE_CMPS" -eq 0 ]; then
        done_generating=0
    fi
fi

if [ "$done_generating" = 1 ]; then
    cd "$UNI"
    rm -rf UniServerApp.auto
    # shellcheck disable=SC2086
    TestFrameworkPath="$TF" "$PY2" "$TF/run.py" \
        --compiler cmake --platform linux --generateOnly $GEN_CONF_ARGS \
        UniServerApp.application
    [ -f "$GEN_CMK" ] || die "генератор не создал UniServerApp.auto/UniServerApp/CMakeLists.txt"
    if [ "$SHIPPING" = 1 ]; then
        grep -q "_SHIPPING" "$GEN_CMK" || die "в сгенерированном CMakeLists.txt нет -D_SHIPPING — shipping-конфигурация не применена"
    fi
    echo "CMakeLists.txt сгенерирован ($([ "$SHIPPING" = 1 ] && echo shipping || echo debug))"
else
    echo "UniServerApp.auto актуален — генерация пропущена"
fi

# ------------------------------------------- 4. сборка UniServerApp
log "4/5. Сборка UniServerApp (cmake + make)"
mkdir -p "$BUILD_DIR"
# Если кэш cmake создан при другом расположении репозитория — сбрасываем
# (иначе в кэше остались чужие абсолютные пути).
GEN_DIR=$UNI/UniServerApp.auto/UniServerApp
if [ -f "$BUILD_DIR/CMakeCache.txt" ] && \
   ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$GEN_DIR" "$BUILD_DIR/CMakeCache.txt"; then
    warn "кэш cmake создан при другом пути — сбрасываю $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi
# CMAKE_POLICY_VERSION_MINIMUM: генератор пишет CMAKE_MINIMUM_REQUIRED(2.8),
# что запрещено CMake 4.x без этого флага.
( cd "$BUILD_DIR" && cmake ../UniServerApp.auto/UniServerApp \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_BUILD_TYPE=Release )
( cd "$BUILD_DIR" && make -j"$(nproc)" )

BIN="$BUILD_DIR/UniServerApp"
[ -x "$BIN" ] || die "не найден сгенерированный бинарник $BIN"

# Контрольный символ vtable ACE_SPIPE_Addr. Нужен для случая, когда vendorный
# libACE-5.7.so собран без SPIPE (оригинальная вендорная сборка) — тогда
# динамический линкер берёт символ из основного бинарника. Если Linux-сборка
# ACE содержит SPIPE (как здесь), символ сам в libACE-5.7.so — всё равно ок.
# NB: grep -q в пайплайне с set -o pipefail ненадёжен (SIGPIPE в nm при раннем
# выходе grep) — используем grep -c, который дочитывает вход до конца.
spipe_exe_dyn=$(nm -D "$BIN" 2>/dev/null | grep -c "_ZTV14ACE_SPIPE_Addr" || true)
spipe_exe=$(nm "$BIN" 2>/dev/null | grep -c "_ZTV14ACE_SPIPE_Addr" || true)
spipe_ace=$(nm -D "$ACE/lib/libACE-5.7.so" 2>/dev/null | grep -c "_ZTV14ACE_SPIPE_Addr" || true)
if [ "${spipe_exe_dyn:-0}" -gt 0 ]; then
    echo "OK: _ZTV14ACE_SPIPE_Addr экспортируется бинарником"
elif [ "${spipe_exe:-0}" -gt 0 ] || [ "${spipe_ace:-0}" -gt 0 ]; then
    echo "OK: _ZTV14ACE_SPIPE_Addr на месте (бинарник/libACE)"
else
    die "символ _ZTV14ACE_SPIPE_Addr не найден ни в бинарнике, ни в libACE-5.7.so (см. BUILD_LINUX.md)"
fi

# Смоук-тест: бинарник должен запускаться без ошибок динамической линковки
# ("symbol lookup error" — главный признак проблемы с ACE_SPIPE_Addr).
# Конфигурационные ошибки при этом допустимы (зависят от каталога запуска).
smoke_test() {  # $1 = бинарник, $2 = каталог запуска
    local out
    out=$( cd "$2" && timeout 5 "$1" coordinator 2>&1 || true )
    if grep -qE "symbol lookup error|undefined symbol|cannot open shared object|error while loading" <<< "$out"; then
        grep -E "symbol lookup error|undefined symbol|cannot open shared object|error while loading" <<< "$out" | head -3
        die "ошибки динамической линковки при запуске UniServerApp"
    fi
    echo "smoke-тест пройден: ошибок линковки/загрузки нет"
}

# ------------------------------------------- 5. развёртывание
if [ "$NO_DEPLOY" = 1 ]; then
    log "5/5. Смоук-тест без развёртывания"
    smoke_test "$BIN" "$BUILD_DIR"
    log "Готово (--no-deploy, бинарник в $BIN)"
    exit 0
fi
log "5/5. Копирование в $DEPLOY_DIR + смоук-тест"
mkdir -p "$DEPLOY_DIR"
cp -f "$BIN" "$DEPLOY_DIR/UniServerApp"
smoke_test "$DEPLOY_DIR/UniServerApp" "$DEPLOY_DIR"

log "СБОРКА ЗАВЕРШЕНА"
cat <<EOF

Бинарник:  $DEPLOY_DIR/UniServerApp
Запуск (обязательно из каталога Bin, иначе не найдутся Profiles):
  cd $DEPLOY_DIR
  ./UniServerApp coordinator     # или: chat, login, relay, lobby, gamebalancer, newlogin ...
  # либо пакетно:
  bash start_all.sh
Остановка:
  bash stop_all.sh
EOF
