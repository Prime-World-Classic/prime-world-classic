#!/usr/bin/env bash
# =============================================================================
# build_linux_vendor.sh — подготовка и сборка вендорных библиотек UniServerApp
# для Linux из ЧИСТОГО клона ветки linux_server.
#
# Что исправляет автоматически:
#   1) configure: error: cannot find required auxiliary files
#      (ltmain.sh, compile, install-sh, config.guess, config.sub)
#      -> создаёт Vendor/ACE_wrappers/aux_config/ из системных autotools-файлов
#   2) config.status: error: cannot find input file: 'Makefile.in'
#      -> генерирует все ~201 Makefile.in из Makefile.am (aclocal + automake)
#   3) (страховка) повреждённые токены 'fidone' в configure -> 'fi; done'
#   4) Собирает ACE 5.7  (libACE-5.7.so, libACE.so)
#      и Terabit         (TProactor.so, IOTerabit.so) и раскладывает их
#      (вместе с symlink'ами, в т.ч. libzdll.so) так, как их ждёт линковщик
#      UniServerApp.
#   5) Если найден Python 2.7 (conda-окружение py27 или python2 в PATH) —
#      дополнительно генерирует CMakeLists (TestFramework) и собирает
#      UniServerApp целиком.
#
# Использование:
#   ./build_linux_vendor.sh               # всё подряд (до UniServerApp, если есть py2.7)
#   ./build_linux_vendor.sh --vendor-only # только вендорные библиотеки (ACE + Terabit)
#
# Требования: Ubuntu/Debian x86_64, root или sudo, сеть (для apt при отсутствии пакетов).
# =============================================================================
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BR=$SCRIPT_DIR
ACE=$BR/Vendor/ACE_wrappers
TB=$BR/Vendor/Terabit
UNI=$BR/Src/Game/PF/UniServer
TF=$BR/Tools/TestFramework

VENDOR_ONLY=0
[ "${1:-}" = "--vendor-only" ] && VENDOR_ONLY=1

log() { echo -e "\n\033[1;32m==> $* ==\033[0m"; }
warn(){ echo -e "\033[1;33mWARN: $*\033[0m"; }
die() { echo -e "\033[1;31mERROR: $*\033[0m" >&2; exit 1; }

[ -d "$ACE" ] || die "не найден каталог Vendor/ACE_wrappers (запускайте скрипт из ветки r1117)"

# ---------------------------------------------------------------- 0. пакеты
log "0/5. Системные зависимости"
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
    SUDO=""; [ "$(id -u)" != "0" ] && SUDO="sudo"
    echo "Устанавливаю:$pkgs"
    $SUDO apt-get update -qq
    # shellcheck disable=SC2086
    $SUDO apt-get install -y $pkgs
else
    echo "Все зависимости на месте"
fi

# ------------------------------------------- 1. aux_config (фикс #1)
log "1/5. Autotools aux-файлы (aux_config)"
if [ -f "$ACE/aux_config/ltmain.sh" ] && [ -f "$ACE/aux_config/compile" ] && [ -f "$ACE/aux_config/config.sub" ]; then
    echo "aux_config уже на месте"
else
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
    miss=""
    for f in ltmain.sh compile install-sh config.guess config.sub; do
        [ -f "$ACE/aux_config/$f" ] || miss="$miss $f"
    done
    [ -z "$miss" ] || die "aux_config неполон:$miss"
    echo "aux_config создан из: $AUXSRC"
fi

# ------------------------------------------- 2. fidone (фикс #3)
log "2/5. Проверка configure (fidone -> fi; done)"
if grep -q '^fidone$' "$ACE/configure"; then
    n=$(grep -c '^fidone$' "$ACE/configure")
    sed -i 's/^fidone$/fi; done/' "$ACE/configure"
    echo "исправлено вхождений: $n"
else
    echo "configure в порядке"
fi

# ------------------------------------------- 3. Makefile.in (фикс #2)
log "3/5. Генерация Makefile.in (aclocal + automake)"
if [ -f "$ACE/ace/Makefile.in" ]; then
    echo "Makefile.in уже сгенерированы"
else
    ( cd "$ACE" && aclocal )
    ( cd "$ACE" && find . -name Makefile.am -not -path './build/*' -print0 \
        | xargs -0 -n1 dirname | sort -u \
        | while IFS= read -r d; do
              ( cd "$ACE/$d" && automake --add-missing --foreign ) \
                  || echo "  (warn) $d: automake завершился с ошибкой (Makefile.in мог быть создан)"
          done )
fi
# проверка: каждый файл из списка configure должен иметь .in
cd "$ACE"
list=$(grep -m1 '^ac_config_files=' configure | sed 's/^ac_config_files="\$ac_config_files //; s/"$//' || true)
[ -n "$list" ] || die "не удалось прочитать список файлов из configure"
missing=""
for f in $list; do
    [ -f "$f.in" ] || missing="$missing $f.in"
done
[ -z "$missing" ] || die "не хватает Makefile.in:$missing"
echo "Все Makefile.in на месте ($(echo $list | wc -w) шт.)"

# ------------------------------------------- 4. ACE 5.7
log "4/5. Сборка ACE 5.7"
mkdir -p "$ACE/build"
cd "$ACE/build"
../configure --disable-tests --disable-samples
make -j"$(nproc)"
ls lib/libACE-5.7.so* >/dev/null 2>&1 || die "не найден результат сборки ACE (lib/libACE-5.7.so*)"
cp -f lib/libACE-5.7.so* ../lib/
ln -sf libACE-5.7.so ../lib/libACE.so
echo "libACE-5.7.so / libACE.so -> Vendor/ACE_wrappers/lib/"

# ------------------------------------------- 5. Terabit
log "5/5. Сборка Terabit (TProactor, IOTerabit)"
cd "$TB"
cmake -S . -B build
cmake --build build -j"$(nproc)"
cp -f build/libTProactor.so lib/TProactor.so
cp -f build/libIOTerabit.so lib/IOTerabit.so
ln -sf TProactor.so lib/libTProactor.so
ln -sf IOTerabit.so lib/libIOTerabit.so
if [ -f /usr/lib/x86_64-linux-gnu/libz.so ]; then
    ln -sf /usr/lib/x86_64-linux-gnu/libz.so lib/libzdll.so
elif [ -f /lib/x86_64-linux-gnu/libz.so ]; then
    ln -sf /lib/x86_64-linux-gnu/libz.so lib/libzdll.so
else
    Z=$(ls /usr/lib/*/libz.so 2>/dev/null | head -1)
    [ -n "$Z" ] || die "не найден системный libz.so"
    ln -sf "$Z" lib/libzdll.so
fi
echo "TProactor.so / IOTerabit.so / libzdll.so -> Vendor/Terabit/lib/"

# ------------------------------------------- 6. UniServerApp (опционально)
if [ "$VENDOR_ONLY" = 1 ]; then
    log "Готово (vendor-only)"
    echo "Дальше вручную: генерация CMakeLists (TestFramework, python2.7) и сборка UniServerApp."
    exit 0
fi

PY2=""
[ -x "$HOME/miniconda3/envs/py27/bin/python" ] && PY2="$HOME/miniconda3/envs/py27/bin/python"
[ -z "$PY2" ] && command -v python2 >/dev/null && PY2="$(command -v python2)"

if [ -z "$PY2" ]; then
    log "Вендорные библиотеки собраны"
    cat <<EOF
Python 2.7 не найден (conda env py27 / python2) — UniServerApp не собирать.
Следующие шаги вручную:
  1) conda:  conda create -n py27 python=2.7 -c conda-forge
  2) cd $UNI
     source ~/miniconda3/etc/profile.d/conda.sh && conda activate py27
     TestFrameworkPath=$TF python $TF/run.py \\
         --compiler cmake --platform linux --generateOnly UniServerApp.application
  3) mkdir -p build_linux_test && cd build_linux_test
     cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ../UniServerApp.auto/UniServerApp
     make -j\$(nproc)
  4) Запуск: cd $BR/../../../../pw_publish/branch/Server/PvX/Bin  (из корня репозитория)
     $UNI/build_linux_test/UniServerApp chat
EOF
    exit 0
fi

log "6/6. Генерация CMakeLists и сборка UniServerApp ($PY2)"
cd "$UNI"
TestFrameworkPath="$TF" "$PY2" "$TF/run.py" \
    --compiler cmake --platform linux --generateOnly UniServerApp.application
mkdir -p build_linux_test
cd build_linux_test
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ../UniServerApp.auto/UniServerApp
make -j"$(nproc)"
log "ГОТОВО"
echo "Бинарник: $UNI/build_linux_test/UniServerApp"
echo "Запуск (cwd обязателен):"
echo "  cd $(cd "$BR/.." && pwd)/../pw_publish/branch/Server/PvX/Bin"
echo "  $UNI/build_linux_test/UniServerApp chat   # или: coordinator clientctrl lobby chat gamebalancer newlogin gamesvc"
