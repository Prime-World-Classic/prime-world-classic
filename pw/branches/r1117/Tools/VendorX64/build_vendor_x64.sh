#!/usr/bin/env bash
# =============================================================================
# build_vendor_x64.sh — пересборка вендорных библиотек клиента под x64
# (этап 3 плана PLAN_client_modern.md).
#
# Вендор в дереве описан под x86 (vcproj + предсобранные .lib). Для x64-клиента
# их надо собрать тем же тулчейном, что и сам клиент: cl 14.44 + Windows Kits 10
# в wine-префиксе ~/.wine-vs (тот же путь, что у build_client_cmake.sh
# --toolchain=vs2022).
#
# Использование:
#   ./build_vendor_x64.sh                 # все известные цели
#   ./build_vendor_x64.sh zlib jpeg       # только перечисленные
#   ./build_vendor_x64.sh --list
#   PW_BUILD_JOBS=8 ./build_vendor_x64.sh
#
# Результат раскладывается по Vendor/<вендор>/lib/x64/ — gen_cmake.py при
# PW_MACHINE=X64 подставляет каталог lib/x64, если он существует (X64_DIR_REMAP),
# поэтому vcproj не правим.
#
# Чего здесь НЕТ: freetype (Vendor/freetype — только include+lib, исходников в
# дереве нет) и OpenSSL (Vendor/OpenSSL — то же). Их источники надо приносить
# снаружи; решение о внешних источниках принимает человек.
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../../../../.." && pwd)"   # корень репозитория
BR="$REPO/pw/branches/r1117"
V="$BR/Vendor"
VX="$BR/Tools/VendorX64"
BUILD="$VX/build"

WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
CMAKE_DIR="${PW_CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"
JOBS="${PW_BUILD_JOBS:-8}"

# ------------------------------------------------------------------ libcurl
# curl 7.22 из дерева (Vendor/libcurl/src/curl-7.22.0). Клиент компилируется с
# CURL_STATICLIB и линкует libcurl.lib статически — собираем STATIC. SSL:
# x86-ный libcurl был собран с OpenSSL (ssleay32MT/libeay32MT в списке линковок),
# x64-ного OpenSSL в дереве нет, поэтому https идёт через Windows Schannel.
gen_curl() {
    local out="$V/libcurl/lib/Release/x64"
    DEST="$out" ARTIFACTS="libcurl.lib"
    SRC="$V/libcurl/src/curl-7.22.0"
    EXTRA="-DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DENABLE_SCHANNEL=ON "
    EXTRA="$EXTRA -DUSE_OPENSSL=OFF -DHTTP_ONLY=ON -DENABLE_MANUAL=OFF -DCURL_STATIC_CRT=ON"
}

# ------------------------------------------------------------------ CrashRpt
# CrashRpt 1.4.02 из дерева (свой CMake). Как и x86 — DLL + import lib.
gen_crashrpt() {
    local out="$V/CrashRpt/lib/x64"
    DEST="$out" ARTIFACTS="CrashRpt.dll CrashRpt.lib"
    SRC="$V/CrashRpt"
    EXTRA="-DCRASHRPT_BUILD_SHARED_LIBS=ON -DCRASHRPT_LINK_CRT_AS_DLL=ON"
}

# ------------------------------------------------------------------ CensorDll
# Самописная censor-библиотека клиента (Tools/Censor), x86-ная — в Tools/Censor/lib.
gen_censor() {
    local out="$BR/Tools/Censor/lib/Release/x64"
    local c="$(zfwd "$BR/Tools/Censor")"
    cat > "$BUILD/censor/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(censor CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
add_library(CensorDll SHARED ${c}/dllmain.cpp ${c}/stdafx.cpp ${c}/CensorTest.cpp)
set_target_properties(CensorDll PROPERTIES OUTPUT_NAME CensorDll)
# CensorTest.cpp тянет клиентский заголовок Game/PF/Server/CensorshipCore/Censor.h
target_include_directories(CensorDll PRIVATE ${c} $(zfwd "$BR/Src") $(zfwd "$V/boost"))
target_compile_definitions(CensorDll PRIVATE CENSORLIB_EXPORT _CRT_SECURE_NO_WARNINGS WIN32 NDEBUG _WINDOWS _UNICODE UNICODE)
target_compile_options(CensorDll PRIVATE /O2 /MT /EHsc /std:c++14 /wd4996 /wd4267)
EOF
    DEST="$out" ARTIFACTS="CensorDll.dll CensorDll.lib"
}

ALL_TARGETS="zlib jpeg jsoncpp ace terabit curl crashrpt censor"

TARGETS=()
for arg in "$@"; do
    case "$arg" in
        --list) echo "$ALL_TARGETS"; exit 0 ;;
        -h|--help) grep '^# ' "$0" | sed 's/^# \{0,1\}//' | head -30; exit 0 ;;
        *) TARGETS+=("$arg") ;;
    esac
done
[ ${#TARGETS[@]} -gt 0 ] || TARGETS=($ALL_TARGETS)

for p in "$CMAKE_DIR/bin/cmake.exe" "$NINJA"; do
    [ -e "$p" ] || { echo "ERROR: не найдено: $p" >&2; exit 1; }
done
ls "$WINEPREFIX/drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/"*/bin/HostX64/x64/cl.exe >/dev/null 2>&1 \
    || { echo "ERROR: в $WINEPREFIX нет MSVC (x64) из VS2022 BuildTools" >&2; exit 1; }
export WINEPREFIX

zpath() { echo "Z:$(echo "$1" | sed 's#/#\\#g')"; }
zfwd()  { echo "Z:$1"; }

MSVCVER="$(basename "$(ls -d "$WINEPREFIX/drive_c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/"*/ | sort -V | tail -1)")"
KVER="$(basename "$(ls -d "$WINEPREFIX/drive_c/Program Files (x86)/Windows Kits/10/Include/"10.*/ | sort -V | tail -1)")"
MSVC_WIN='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
KITS_WIN='C:\Program Files (x86)\Windows Kits\10'
MSVC_WIN="$MSVC_WIN\\$MSVCVER"
CL_PATH="$MSVC_WIN\\bin\\HostX64\\x64;$KITS_WIN\\bin\\x64"
CL_INC="$MSVC_WIN\\include;$KITS_WIN\\Include\\${KVER}\\ucrt;$KITS_WIN\\Include\\${KVER}\\um;$KITS_WIN\\Include\\${KVER}\\shared"
CL_LIB="$MSVC_WIN\\lib\\x64;$KITS_WIN\\Lib\\${KVER}\\ucrt\\x64;$KITS_WIN\\Lib\\${KVER}\\um\\x64"
echo "== тулчейн: MSVC $MSVCVER, Windows Kits $KVER (x64)"

# ------------------------------------------------------------------ zlib
# Источник — Vendor/CrashRpt/thirdparty/zlib (в Vendor/zlib исходников нет).
# Собирается DLL+import lib ровно в той же форме, что x86-ный zlib1.dll/zdll.lib
# (экспорты — Vendor/zlib/lib/zlib.def), чтобы BIn/ не менялся по составу.
gen_zlib() {
    local out="$V/zlib/lib/x64"
    local zsrc="$(zfwd "$V/CrashRpt/thirdparty/zlib")"
    cat > "$BUILD/zlib/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(zlib C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
include_directories($(zfwd "$V/CrashRpt/thirdparty/zlib") $(zfwd "$V/zlib/include"))
add_library(zlib SHARED
  $zsrc/adler32.c $zsrc/compress.c $zsrc/crc32.c $zsrc/deflate.c $zsrc/gzclose.c
  $zsrc/gzlib.c $zsrc/gzread.c $zsrc/gzwrite.c $zsrc/infback.c $zsrc/inffast.c
  $zsrc/inflate.c $zsrc/inftrees.c $zsrc/trees.c $zsrc/uncompr.c $zsrc/zutil.c)
set_target_properties(zlib PROPERTIES
  OUTPUT_NAME zlib1
  LINK_FLAGS "/DEF:$(zfwd "$V/zlib/lib/zlib.def") /MACHINE:X64")
# zlib 1.2.5 в gz*.c использует POSIX read/write/close/open/lseek — UCRT их не
# экспортирует (только _read/_write/...), поэтому маппинг через дефайны.
target_compile_definitions(zlib PRIVATE ZLIB_DLL _CRT_SECURE_NO_WARNINGS
  open=_open read=_read write=_write close=_close lseek=_lseek)
target_compile_options(zlib PRIVATE /O2 /MT /wd4996 /wd4267 /wd4244 /wd4100 /wd4131 /wd4018)
EOF
    DEST="$out" ARTIFACTS="zlib1.dll zlib1.lib"
    RENAME="zlib1.lib=zdll.lib"   # клиент линкует zdll.lib (как x86)
}

# ------------------------------------------------------------------ jpeg
# Источник — Vendor/CrashRpt/thirdparty/jpeg (46 .c), заголовки — Vendor/jpeglib/include
# (в нём же jconfig.h, которым этот jpeg собран). Статическая либа, как x86 jpeglib.lib.
gen_jpeg() {
    local out="$V/jpeglib/lib/x64"
    cat > "$BUILD/jpeg/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(jpeg C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
include_directories($(zfwd "$V/CrashRpt/thirdparty/jpeg") $(zfwd "$V/jpeglib/include"))
file(GLOB JPEG_SRC $(zfwd "$V/CrashRpt/thirdparty/jpeg")/*.c)
add_library(jpeglib STATIC \${JPEG_SRC})
set_target_properties(jpeglib PROPERTIES OUTPUT_NAME jpeglib)
target_compile_definitions(jpeglib PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_compile_options(jpeglib PRIVATE /O2 /MT /wd4996 /wd4267 /wd4244 /wd4100 /wd4018 /wd4131 /wd4715 /wd4701)
EOF
    DEST="$out" ARTIFACTS="jpeglib.lib"
}

# ------------------------------------------------------------------ ACE
# ACE 5.7 (Vendor/ACE_wrappers) — x86-ный ACE.lib в дереве это import lib для
# ACE.dll, поэтому собираем SHARED. Исходниковый набор — все ace/*.cpp
# (платформенно-лишние отсекаются #ifdef в самих файлах).
gen_ace() {
    local out="$V/ACE_wrappers/lib/x64"
    local a="$(zfwd "$V/ACE_wrappers")"
    local ACE_INCL
    # «не самостоятельные» .cpp: те, которые include-ются ЧУГИМИ файлами
    # (Cleanup.cpp ← OS.cpp, OS_NS_* ← их .h c ACE_TEMPLATES_REQUIRE_SOURCE …).
    # Файл, включаемый только собственным заголовком (String_Base.cpp ← String_Base.h),
    # наоборот, компилируем: в нём явные инстанциации `template class ACE_Export …`,
    # без них ACE.dll не экспортирует эти шаблоны, и Terabit не линкуется
    # (__imp_?…ACE_String_Base…).
    ACE_INCL="$(
      cd "$V/ACE_wrappers/ace" || exit 1
      for f in *.cpp; do
        b="${f%.cpp}"
        inc="$(grep -lE "#include "ace/$f"" *.h *.inl *.cpp 2>/dev/null | grep -v "^$b\.h$" | head -1)"
        [ -n "$inc" ] && echo "$b"
      done | paste -sd'|' -
    )"
    ACE_INCL="${ACE_INCL:-__none__}"
    cat > "$BUILD/ace_posix_shim.c" <<'SHIM'
/* x64 UCRT не даёт POSIX-имён, которые использует ACE 5.7 (OLDNAMES.lib —
   только x86). Тонкие обёртки, см. PLAN_client_modern.md, этап 3. */
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
int    access (const char *path, int mode)          { return _access(path, mode); }
int    unlink (const char *path)                    { return _unlink(path); }
int    rmdir  (const char *path)                    { return _rmdir(path); }
char  *strdup (const char *s)                       { return _strdup(s); }
FILE  *fdopen (int fd, const char *mode)            { return _fdopen(fd, mode); }
SHIM
    SHIM_WIN="$(zfwd "$BUILD/ace_posix_shim.c")"
    cat > "$BUILD/ace/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(ace C CXX)   # C обязателен: без него CMake молча игнорирует .c-источник (шим)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
# ВАЖНО: в include path НЕ добавляем $a/ace — иначе wine (case-insensitive)
# подставляет ACE'шный ace/Process.h вместо <process.h> из CRT и вся цепочка
# config-win32-* разваливается (os_time.h(30) C1083 и каскад C4430/C2086).
include_directories($a)
file(GLOB ACE_SRC $a/ace/*.cpp)
# ace/Caching_Strategies_T.cpp — в этом вендоре сломан include-guard
# (#ifndef ACE_CACHING_STRATEGIES_T_CPP / #define ACECACHING_STRATEGIES_T_CPP),
# из-за чего определения шаблонов попадают дважды (C2995). Файл только
# шаблонный, для линковки не нужен — исключаем.
# Все ace/*_T.cpp — это «источники шаблонов», которые сами #include-ются своими
# же заголовками (ACE_TEMPLATES_REQUIRE_SOURCE): отдельной компиляции они не
# требуют, а при отдельной компиляции дают C2995 («already defined»). В штатной
# сборке ACE (MPC) они в список источников не входят.
# Второй класс «не самостоятельных» источников: ace/*.cpp, которые #include-ются
# заголовками/другими .cpp (Cleanup.cpp, OS_NS_*.cpp, Module.cpp, …). Их тоже не
# компилируем отдельно, иначе LNK2005 (уже определено в двух obj). Список
# вычисляется из самого дерева.
set(ACE_INCL ${ACE_INCL})
# ace/*_T.cpp — «источники шаблонов»: их определения дублируют .inl, которые
# включены в тот же заголовок → C2995 при отдельной компиляции. В штатной сборке
# ACE (MPC) в список источников не входят.
list(FILTER ACE_SRC EXCLUDE REGEX "_T\\.cpp$")
# «/» в начале обязательно: без якоря альтернатива Stream вырезала бы и
# CDR_Stream.cpp (имена сравниваются по суффиксу).
list(FILTER ACE_SRC EXCLUDE REGEX "/(${ACE_INCL})\\.cpp$")
# ace/SPIPE_*.cpp (легаси-транспорт «emulated pipes») и ace/UPIPE_*.cpp (Unix
# domain pipes, наследуются от ACE_SPIPE_*): на Windows в штатной сборке ACE не
# входят; компиляция их .cpp даёт C2511/C2039, а линковка — LNK2019 на ACE_SPIPE_*.
# Клиент/Terabit ни SPIPE, ни UPIPE не используют.
list(FILTER ACE_SRC EXCLUDE REGEX "/(SPIPE|UPIPE)[A-Za-z0-9_]*\\.cpp$")
# POSIX-имена CRT (access/fdopen/strdup/unlink/rmdir): на x64 UCRT их не
# экспортирует (OLDNAMES.lib — только x86), а /D-макросы ломают собственные
# обёртки ACE_OS::fdopen (C2039). Поэтому — тонкий шим отдельным файлом.
# шим задаётся ОТНОСИТЕЛЬНО CMakeLists: CMake молча игнорирует источник с
# диском вида Z:/… (в rsp его нет → POSIX-имена остаются незакрытыми)
add_library(ACE SHARED \${ACE_SRC} ../ace_posix_shim.c)
set_target_properties(ACE PROPERTIES OUTPUT_NAME ACE)
# ACE_BUILD_DLL — без него ACE_Export разворачивается в dllimport и
# определение ACE_Addr::sap_any (ACE/Addr.cpp) не линкуется (C2491).
target_compile_definitions(ACE PRIVATE ACE_BUILD_DLL ACE_NTRACE=1 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE WIN32 _WINDOWS NDEBUG)
target_compile_options(ACE PRIVATE /O2 /MT /EHsc /GR /std:c++14 /wd4996 /wd4267 /wd4244 /wd4100 /wd4101 /wd4189 /wd4018 /wd4091 /wd4819)
target_link_libraries(ACE PRIVATE ws2_32 iphlpapi advapi32 user32)
EOF
    DEST="$out" ARTIFACTS="ACE.dll ACE.lib"
}

# ------------------------------------------------------------------ Terabit
# Terabit (Vendor/Terabit) — Windows-сборка: TProactor.dll + IOTerabit.dll
# (в дереве x86-ные import lib'ы). CMakeLists вендора — под POSIX (серверный
# порт), для клиента нужны Win32-источники, поэтому список свой.
gen_terabit() {
    local out="$V/Terabit/lib/x64"
    local t="$(zfwd "$V/Terabit")"
    local a="$(zfwd "$V/ACE_wrappers")"
    local ACE_INCL
    # «не самостоятельные» .cpp: те, которые include-ются ЧУГИМИ файлами
    # (Cleanup.cpp ← OS.cpp, OS_NS_* ← их .h c ACE_TEMPLATES_REQUIRE_SOURCE …).
    # Файл, включаемый только собственным заголовком (String_Base.cpp ← String_Base.h),
    # наоборот, компилируем: в нём явные инстанциации `template class ACE_Export …`,
    # без них ACE.dll не экспортирует эти шаблоны, и Terabit не линкуется
    # (__imp_?…ACE_String_Base…).
    ACE_INCL="$(
      cd "$V/ACE_wrappers/ace" || exit 1
      for f in *.cpp; do
        b="${f%.cpp}"
        inc="$(grep -lE "#include "ace/$f"" *.h *.inl *.cpp 2>/dev/null | grep -v "^$b\.h$" | head -1)"
        [ -n "$inc" ] && echo "$b"
      done | paste -sd'|' -
    )"
    ACE_INCL="${ACE_INCL:-__none__}"
    cat > "$BUILD/ace_posix_shim.c" <<'SHIM'
/* x64 UCRT не даёт POSIX-имён, которые использует ACE 5.7 (OLDNAMES.lib —
   только x86). Тонкие обёртки, см. PLAN_client_modern.md, этап 3. */
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
int    access (const char *path, int mode)          { return _access(path, mode); }
int    unlink (const char *path)                    { return _unlink(path); }
int    rmdir  (const char *path)                    { return _rmdir(path); }
char  *strdup (const char *s)                       { return _strdup(s); }
FILE  *fdopen (int fd, const char *mode)            { return _fdopen(fd, mode); }
SHIM
    SHIM_WIN="$(zfwd "$BUILD/ace_posix_shim.c")"
    local c="$t"
    cat > "$BUILD/terabit/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(terabit CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
include_directories($t $t/TProactor $t/app $a)
file(GLOB TPROACTOR_SRC $t/TProactor/*.cpp)
list(FILTER TPROACTOR_SRC EXCLUDE REGEX "POSIX|posix")
add_library(TProactor SHARED \${TPROACTOR_SRC})
file(GLOB IOT_SRC $t/app/IOTerabit/*.cpp)
add_library(IOTerabit SHARED \${IOT_SRC})
target_include_directories(IOTerabit PRIVATE ${c} ${c}/TProactor)
target_compile_definitions(IOTerabit PRIVATE IOTERABIT_BUILD_DLL AIO_ROOT _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE WIN32 NDEBUG)
target_compile_options(IOTerabit PRIVATE /O2 /MT /EHsc /GR /std:c++14 /wd4996 /wd4267 /wd4244 /wd4100 /wd4101 /wd4189 /wd4018 /wd4099)
target_link_libraries(IOTerabit PRIVATE TProactor $a/lib/x64/ACE.lib ws2_32)
target_compile_definitions(TProactor PRIVATE TPROACTOR_BUILD_DLL AIO_ROOT _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE WIN32 NDEBUG)
target_compile_options(TProactor PRIVATE /O2 /MT /EHsc /GR /std:c++14 /wd4996 /wd4267 /wd4244 /wd4100 /wd4101 /wd4189 /wd4018 /wd4099)
target_link_libraries(TProactor PRIVATE $a/lib/x64/ACE.lib ws2_32)
EOF
    DEST="$out" ARTIFACTS="TProactor.dll TProactor.lib IOTerabit.dll IOTerabit.lib"
}

# ------------------------------------------------------------------ JsonCpp
gen_jsoncpp() {
    local out="$V/JsonCpp/lib/Release/x64"
    cat > "$BUILD/jsoncpp/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(jsoncpp CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
include_directories($(zfwd "$V/JsonCpp/include") $(zfwd "$V/JsonCpp/src"))
add_library(JsonCpp STATIC
  $(zfwd "$V/JsonCpp/src/lib_json/json_reader.cpp")
  $(zfwd "$V/JsonCpp/src/lib_json/json_value.cpp")
  $(zfwd "$V/JsonCpp/src/lib_json/json_writer.cpp"))
set_target_properties(JsonCpp PROPERTIES OUTPUT_NAME JsonCpp)
target_compile_definitions(JsonCpp PRIVATE JSON_DLL_STATIC WIN32 NDEBUG _CRT_SECURE_NO_WARNINGS)
target_compile_options(JsonCpp PRIVATE /O2 /MT /GS- /GF /std:c++14 /wd4996 /wd4267 /wd4244 /wd4100 /wd4251 /wd4275)
EOF
    DEST="$out" ARTIFACTS="JsonCpp.lib"
}

build_one() {
    local t="$1"
    echo "== $t"
    mkdir -p "$BUILD/$t"
    # каталог сборки НЕ вычищаем (иначе 390 TU ACE пересобираются ради шага
    # линковки); полный сброс — PW_VENDOR_FRESH=1
    ARTIFACTS=""; DEST=""; RENAME=""; SRC=""; EXTRA=""
    "gen_$t"
    local bwin="$(zpath "$BUILD/$t")"
    local srcarg="."
    if [ -n "${SRC:-}" ]; then
        srcarg="$(zfwd "$SRC")"
    fi
    cat > "$VX/vendor_env.cmd" <<EOF
@echo off
set PATH=$(zpath "$CMAKE_DIR/bin");$(zpath "$(dirname "$NINJA")");$CL_PATH;%PATH%
set INCLUDE=$CL_INC
set LIB=$CL_LIB
set WINEDEBUG=-all
cd /d $bwin
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 %EXTRA% ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" -DCMAKE_MAKE_PROGRAM=$(zfwd "$NINJA") %SRCARG%
if errorlevel 1 exit /b 1
cmake --build . -- -j$JOBS -k0
EOF
    sed -i "s|%EXTRA%|${EXTRA:-}|; s|%SRCARG%|${srcarg}|" "$VX/vendor_env.cmd"
    WINEDEBUG=-all wine cmd /c "$(zpath "$VX/vendor_env.cmd")"
    mkdir -p "$DEST"
    for a in $ARTIFACTS; do
        local found
        found="$(find "$BUILD/$t" -name "$a" -print -quit 2>/dev/null)"
        if [ -n "$found" ]; then
            local dst="$a"
            for r in $RENAME; do
                case "$r" in "$a="*) dst="${r#*=}" ;; esac
            done
            cp -f "$found" "$DEST/$dst"
            echo "   -> ${dst}: $(stat -c%s "$DEST/$dst") Б"
        else
            echo "   !! артефакт не найден: $a" >&2
            return 1
        fi
    done
}

fail=0
for t in "${TARGETS[@]}"; do
    build_one "$t" || fail=1
done
exit $fail
