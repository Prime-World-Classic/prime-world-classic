#!/usr/bin/env bash
# =============================================================================
# run_fp_smoke.sh — побитовый смоук трансцендентных x64 под Wine (тулчейн тот же,
# что у build_client_x64_wine.sh: MSVC из ~/.wine-vs).
#
# Что проверяет (контекст — Src/System/FpDeterminism.cpp, fp_smoke.cpp):
#   x64 CRT выбирает sin/cos/pow/exp по CPUID (FMA3 / не FMA3). Консенсус клиентов
#   держится на том, что биты совпадают, значит фиксированный набор проб обязан
#   давать один и тот же хэш на любой машине.
#
# Шаг 1. собираются ТРИ варианта стенда (флаги = флаги x64-графа клиента):
#   strict-off   /fp:strict, FMA3-диспетч CRT выключен   ← то же, что в клиенте
#   strict-on    /fp:strict, CRT по умолчанию (FMA3 как выбрал CPUID)
#   precise-off  без /fp:strict (дефолт cl = /fp:precise), FMA3 выключен
# Шаг 2. сверка хэшей:
#   strict-off  == precise-off   → /fp:strict не трогает биты таблицы (так и надо:
#                                  таблица — вызовы CRT, а не арифметика cl)
#   strict-off  != strict-on     → FMA3-диспетч реально меняет биты, т.е.
#                                  _set_FMA3_enable(0) обязателен
#   (на не-FMA CPU совпадение off/on нормально — там меняться нечему)
# Шаг 3. strict-off сверяется с golden_x64.txt; --update пересоздаёт эталон.
#
# Использование:
#   ./run_fp_smoke.sh                 сборка + смоук + сверка с эталоном
#   ./run_fp_smoke.sh --update        пересоздать golden_x64.txt из этого прогона
#   ./run_fp_smoke.sh --bits          полные дампы проб в out/*.bits.txt
#   ./run_fp_smoke.sh --prefix=PATH   другой Wine-префикс
#   ./run_fp_smoke.sh --no-build      только прогон уже собранных .exe
#
# ВАЖНО про эталон: под Wine ucrtbase — реализация Wine, а не Microsoft. Она
# соблюдает _set_FMA3_enable (проверено: cos(1) расходится на 1 ULP), но её биты
# НЕ обязаны совпадать с настоящим Windows. golden_x64.txt отсюда — регрессионная
# база нашей сборки; финальная сверка «как у игроков» делается на реальной
# Windows-машине (fp_smoke.cmd: tiny10 + любая вторая машина, затем diff хэшей).
# =============================================================================
set -euo pipefail

SMOKE="$(cd "$(dirname "$0")" && pwd)"
BR="$(cd "$SMOKE/../../.." && pwd)"          # pw/branches/r1117
GOLDEN="$SMOKE/golden_x64.txt"
OUT="$SMOKE/out"

DO_UPDATE=0; DO_BITS=0; DO_BUILD=1; PREFIX_FORCED=0
WINEPREFIX="${WINEPREFIX:-}"
for arg in "$@"; do
    case "$arg" in
        --update)     DO_UPDATE=1 ;;
        --bits)       DO_BITS=1 ;;
        --no-build)   DO_BUILD=0 ;;
        --prefix=*)   WINEPREFIX="${arg#--prefix=}"; PREFIX_FORCED=1 ;;
        -h|--help)    grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -40; exit 0 ;;
        *) echo "Неизвестный аргумент: $arg (см. --help)" >&2; exit 2 ;;
    esac
done

# ------------------------------------------------------------- Wine-префикс
VS22_UNIX='drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools'
KITS_UNIX='drive_c/Program Files (x86)/Windows Kits/10'
prefix_ok() {
    local p="$1"
    [ -d "$p" ] || return 1
    ls "$p/$VS22_UNIX/VC/Tools/MSVC/"*/bin/HostX64/x64/cl.exe >/dev/null 2>&1 || return 1
    ls -d "$p/$KITS_UNIX/Include/"10.* >/dev/null 2>&1 || return 1
    return 0
}
if [ "$PREFIX_FORCED" != 1 ] && [ -z "$WINEPREFIX" ]; then
    for cand in "$HOME/.wine-vs" "$HOME/pwbuild/wine32" "$HOME/.wine" \
                $(ls -d "$HOME"/*/wine* "$HOME"/.wine* 2>/dev/null); do
        if prefix_ok "$cand"; then WINEPREFIX="$cand"; break; fi
    done
fi
prefix_ok "${WINEPREFIX:-/nonexistent}" || {
    echo "ERROR: в префиксе '${WINEPREFIX:-?}' нет VS2022 (x64) + Windows Kits 10" >&2
    exit 1
}
export WINEPREFIX
MSVCVER="$(basename "$(ls -d "$WINEPREFIX/$VS22_UNIX/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
KVER="$(basename "$(ls -d "$WINEPREFIX/$KITS_UNIX/Include/"10.*/ | sort -V | tail -1)")"
MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
CL_PATH="$MSVC_WIN\\$MSVCVER\bin\HostX64\x64;$KITS_WIN\bin\x64"
CL_INC="$MSVC_WIN\\$MSVCVER\include;$KITS_WIN\Include\\${KVER}\ucrt;$KITS_WIN\Include\\${KVER}\um;$KITS_WIN\Include\\${KVER}\shared"
CL_LIB="$MSVC_WIN\\$MSVCVER\lib\x64;$KITS_WIN\Lib\\${KVER}\ucrt\x64;$KITS_WIN\Lib\\${KVER}\um\x64"
echo "== Wine-префикс: $WINEPREFIX (MSVC $MSVCVER, Kits $KVER)"

zpath() { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }
mkdir -p "$OUT"

# --------------------------------------------------------------- 1. сборка
# Флаги совпадают с x64-графом клиента (gen_cmake.py): /O2 /Ob2 /MT /GS- /GF /W4
# /WX-. Отдельного /I нет: заголовок таблицы подключается относительным include.
if [ "$DO_BUILD" = 1 ]; then
    cat > "$SMOKE/_build.cmd" <<'CMDEOF'
@echo off
set PATH=__CL_PATH__;%PATH%
set INCLUDE=__CL_INC__
set LIB=__CL_LIB__
set WINEDEBUG=-all
cd /d __SMOKE_WIN__
if not exist out mkdir out
set COMMON=/nologo /O2 /Ob2 /MT /GS- /GF /W4 /WX-
cl %COMMON% /fp:strict fp_smoke.cpp /Feout\fp_strict_off.exe   /link /SUBSYSTEM:CONSOLE version.lib || exit /b 1
cl %COMMON% /fp:strict fp_smoke.cpp /Feout\fp_strict_on.exe    /link /SUBSYSTEM:CONSOLE version.lib || exit /b 1
cl %COMMON%           fp_smoke.cpp /Feout\fp_precise_off.exe   /link /SUBSYSTEM:CONSOLE version.lib || exit /b 1
CMDEOF
    python3 - "$SMOKE/_build.cmd" "$CL_PATH" "$CL_INC" "$CL_LIB" "$(zpath "$SMOKE")" <<'PY'
import sys
p, clpath, clinc, cllib, smoke = sys.argv[1:6]
s = open(p, encoding="utf-8").read()
for k, v in {"__CL_PATH__": clpath, "__CL_INC__": clinc, "__CL_LIB__": cllib,
             "__SMOKE_WIN__": smoke}.items():
    s = s.replace(k, v)
open(p, "w", encoding="utf-8").write(s)
PY
    echo "== 1/3. сборка трёх вариантов стенда"
    if ! WINEDEBUG=-all timeout 900 wine cmd /c "$(zpath "$SMOKE/_build.cmd")" > "$OUT/build.log" 2>&1; then
        echo "ERROR: сборка fp_smoke не удалась:" >&2
        grep -v "err:" "$OUT/build.log" | tail -30 >&2
        exit 1
    fi
    rm -f "$SMOKE/_build.cmd" "$SMOKE"/*.obj
    grep -v "err:" "$OUT/build.log" | tail -5
fi

# ------------------------------------------------- 2. прогоны и взаимные сверки
run() {   # $1 = exe, $2 = аргументы
    WINEDEBUG=-all timeout 120 wine "$OUT/$1" $2 2>/dev/null | grep -v "err:" || true
}

echo "== 2/3. прогоны"
run fp_strict_off.exe ""                 > "$OUT/strict_off.txt"
run fp_strict_off.exe "--fma-default"    > "$OUT/strict_on.txt"
run fp_precise_off.exe ""                > "$OUT/precise_off.txt"
cat "$OUT/strict_off.txt"
if [ "$DO_BITS" = 1 ]; then
    run fp_strict_off.exe "--bits" > "$OUT/strict_off.bits.txt"
    run fp_strict_off.exe "--bits --fma-default" > "$OUT/strict_on.bits.txt"
    echo "-- полные дампы: $OUT/strict_off.bits.txt, $OUT/strict_on.bits.txt"
fi

h_off="$(grep -m1 '^FP-HASH=' "$OUT/strict_off.txt" || true)"
h_on="$(grep -m1 '^FP-HASH=' "$OUT/strict_on.txt" || true)"
h_pre="$(grep -m1 '^FP-HASH=' "$OUT/precise_off.txt" || true)"
cpu="$(grep -m1 '^cpuFMA3=' "$OUT/strict_off.txt" || true)"
[ -n "$h_off" ] && [ -n "$h_on" ] && [ -n "$h_pre" ] || {
    echo "ERROR: стенд не выдал FP-HASH (см. $OUT/*.txt)" >&2; exit 1; }

rc=0
echo
echo "== 3/3. сверки"
if [ "$h_off" = "$h_pre" ]; then
    echo "OK   /fp:strict не меняет биты таблицы:  $h_off"
else
    echo "FAIL /fp:strict изменил биты таблицы:    $h_off vs $h_pre"
    echo "     (набор — вызовы CRT; отличие = сломанный стенд либо другой CRT)"
    rc=1
fi
if [ "$h_off" != "$h_on" ]; then
    echo "OK   FMA3-диспетч CRT меняет биты ($cpu):"
    echo "       с защитой: $h_off"
    echo "       без защиты: $h_on   ⇒ _set_FMA3_enable(0) необходим"
elif [ "${cpu#cpuFMA3=}" = "1" ]; then
    echo "FAIL на FMA3-машине биты с защитой и без совпали —"
    echo "     _set_FMA3_enable(0) не подействовал (CRT не подчиняется?)"
    rc=1
else
    echo "--   FMA3 нет ($cpu): сравнение off/on неприменимо, меняться нечему"
fi

# ------------------------------------------------------- эталон golden_x64.txt
grep -E '^(SAMPLES|FP-HASH|FP-CTRL|FP-DIFF)=' "$OUT/strict_off.txt" > "$OUT/current.txt"
if [ "$DO_UPDATE" = 1 ]; then
    {
        echo "# golden_x64.txt — эталон побитового смоука трансцендентных x64."
        echo "# Режим: /fp:strict + _set_FMA3_enable(0) — то же, что в клиенте."
        echo "# Обновление: Tools/ClientCMake/fp_smoke/run_fp_smoke.sh --update"
        echo "# ВНИМАНИЕ: эталон привязан к CRT. Под Wine ucrtbase — реализация"
        echo "# Wine; на Windows значения обязан подтверждать прогон fp_smoke.cmd"
        echo "# на реальной машине (и сверка между двумя машинами)."
        echo "# Справочно (не сверяется): $(grep -m1 '^cpuFMA3=' "$OUT/strict_off.txt") | $(grep -m1 '^crt:' "$OUT/strict_off.txt") | $(grep -m1 '^fp_smoke:' "$OUT/strict_off.txt")"
        cat "$OUT/current.txt"
    } > "$GOLDEN"
    echo "== эталон пересоздан: $GOLDEN"
elif [ -f "$GOLDEN" ]; then
    if diff -u <(grep -E '^(SAMPLES|FP-HASH|FP-CTRL|FP-DIFF)=' "$GOLDEN") "$OUT/current.txt"; then
        echo "OK   хэши совпадают с эталоном ($(basename "$GOLDEN"))"
    else
        echo "FAIL хэш стенда отличается от эталона — разбор: --bits и diff между машинами"
        rc=1
    fi
else
    echo "--   эталон $GOLDEN нет — создать: run_fp_smoke.sh --update"
    rc=1
fi

echo
echo "Клиент: строка 'fp: ...' в <каталог exe>\\fastfail.log; hash= сверить с $h_off"
exit $rc
