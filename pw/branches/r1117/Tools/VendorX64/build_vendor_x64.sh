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
# внешние источники вендоров (не в репозитории): jpeg 6b, freetype, OpenSSL.
# URL+sha256 — у каждого gen_*; распаковка: см. PLAN_client_modern.md, этап 3.
VSRC="${VSRC:-$HOME/pwbuild/vendor-src}"

WINEPREFIX="${WINEPREFIX:-$HOME/.wine-vs}"
CMAKE_DIR="${PW_CMAKE_DIR:-$HOME/pwbuild/wintools/cmake-3.31.6-windows-x86_64}"
NINJA="${PW_NINJA:-$HOME/pwbuild/wintools/ninja.exe}"
JOBS="${PW_BUILD_JOBS:-8}"

# ------------------------------------------------------------------ libcurl
# curl 7.22 из дерева (Vendor/libcurl/src/curl-7.22.0). Клиент компилируется с
# CURL_STATICLIB и линкует libcurl.lib статически — собираем STATIC. SSL:
# x86-ный libcurl был собран с OpenSSL (ssleay32MT/libeay32MT в списке линковок),
# x64-ного OpenSSL в дереве нет, поэтому https идёт через Windows Schannel.
# ------------------------------------------------------------------ libcurl
# curl 7.22.0 из дерева (Vendor/libcurl/src/curl-7.22.0). Штатный CMake вендора
# неполный (нет CMake/Utilities.cmake), Makefile.msc отсутствует — поэтому свой
# список TU. x86-ная либа в дереве — СТАТИЧЕСКАЯ libcurl.lib (CURL_STATICLIB),
# значит и x64 — статическая. Конфиг — lib/config-win32.h (подхватывается
# curl_setup.h сам при WIN32+BUILDING_LIBCURL).
gen_curl() {
    local out="$V/libcurl/lib/Release/x64"
    local c="$V/libcurl/src/curl-7.22.0"
    [ -f "$c/lib/easy.c" ] || { echo "   !! нет источников curl: $c" >&2; return 1; }
    cat > "$BUILD/curl/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(curl C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
include_directories($(zfwd "$c/include") $(zfwd "$c/lib"))
file(GLOB CURL_SRC $(zfwd "$c/lib")/*.c)
# GSSAPI/SPNEGO (нет gssapi.h на Windows; config-win32.h его и не включает) и
# darwinssl (Apple) в win32-сборку не входят.
list(FILTER CURL_SRC EXCLUDE REGEX "/(gssapi|spnego|darwinssl)\\.c\$")
add_library(libcurl STATIC \${CURL_SRC})
set_target_properties(libcurl PROPERTIES OUTPUT_NAME libcurl)
# LDAP: config-win32.h включает USE_WIN32_LDAP -> 15 отсылок на wldap32
# (__imp_ldap_*), которой в списке либ клиента нет. LDAP клиенту не нужен.
target_compile_definitions(libcurl PRIVATE BUILDING_LIBCURL CURL_STATICLIB CURL_DISABLE_LDAP _CRT_SECURE_NO_WARNINGS)
target_compile_options(libcurl PRIVATE /O2 /MT /wd4018 /wd4090 /wd4100 /wd4127 /wd4244 /wd4245 /wd4267 /wd4701 /wd4996)
EOF
    DEST="$out" ARTIFACTS="libcurl.lib"
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
    # CensorshipCore .cpp рассчитывают на PCH клиента (в их заголовках <string>
    # нет, а std::wstring используется) — подставляем прелюдию.
    printf '#include <string>\n' > "$BUILD/censor/prelude.h"
    local prelude="$(zfwd "$BUILD/censor/prelude.h")"
    cat > "$BUILD/censor/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(censor CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
# CensorDll — тонкая обёртка поверх CensorshipCore (Src/Game/PF/Server/CensorshipCore)
# и nstl-ассоциативных контейнеров (Src/System/ntree.cpp): в x86 эти символы
# брались из CensorDll.lib, который линковался с ними сам. Их тоже компилируем
# внутрь DLL, иначе 11 незакрытых символов (CensorFilter::Censor*, nstl::_Rebalance).
add_library(CensorDll SHARED ${c}/dllmain.cpp ${c}/stdafx.cpp ${c}/CensorTest.cpp
  $(zfwd "$BR/Src/Game/PF/Server/CensorshipCore/Censor.cpp")
  $(zfwd "$BR/Src/Game/PF/Server/CensorshipCore/CensorAsyncManager.cpp")
  $(zfwd "$BR/Src/System/ntree.cpp"))
set_target_properties(CensorDll PROPERTIES OUTPUT_NAME CensorDll)
# CensorTest.cpp тянет клиентский заголовок Game/PF/Server/CensorshipCore/Censor.h
target_include_directories(CensorDll PRIVATE ${c} $(zfwd "$BR/Src") $(zfwd "$V/boost"))
target_compile_definitions(CensorDll PRIVATE CENSORLIB_EXPORT _CRT_SECURE_NO_WARNINGS WIN32 NDEBUG _WINDOWS _UNICODE UNICODE)
target_compile_options(CensorDll PRIVATE /O2 /MT /EHsc /std:c++14 /wd4996 /wd4267 "/FI$prelude")
EOF
    DEST="$out" ARTIFACTS="CensorDll.dll CensorDll.lib"
}

# Tamarin (AVM2) — 99 из 211 незакрытых символов x64-клиента. x86-ная либа —
# СТАТИЧЕСКАЯ Vendor/Tamarin/lib/Release/Tamarin.lib, поэтому и x64 — статическая.
# Конфигурация x64 в этом вендоре САМА: platform/system-selection.h при _M_X64
# определяет AVMSYSTEM_AMD64/SIXTYFOURBIT -> AVMSYSTEM_64BIT=1 (клиент видит тот
# же заголовок, поэтому ABI Atom=64 сходится с Src/UI/FlashInterface.h).
gen_tamarin() {
    local s="$BR/Vendor/Tamarin/source"
    DEST="$BR/Vendor/Tamarin/lib/Release/x64"
    ARTIFACTS="Tamarin.lib"
    local sw="$(zfwd "$s")"
    cat > "$BUILD/tamarin/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(tamarin C CXX ASM_MASM)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
# include-пути повторяют AdditionalIncludeDirectories клиента (model.json),
# иначе клиентские TU и Tamarin.lib увидят разные раскладки.
include_directories($sw/core $sw/MMgc $sw/VMPI $sw/nanojit $sw/pcre $sw/platform
  $sw/eval $sw/extensions $sw/shell $sw/vprof $BR/Vendor/Tamarin)
file(GLOB TAM_CORE $sw/core/*.cpp)
file(GLOB TAM_MMG  $sw/MMgc/*.cpp)
file(GLOB TAM_VMPI $sw/VMPI/*.cpp)
file(GLOB TAM_NJ   $sw/nanojit/*.cpp)
file(GLOB TAM_PCRE $sw/pcre/*.cpp)
file(GLOB TAM_PL   $sw/platform/win32/*.cpp)
list(FILTER TAM_MMG  EXCLUDE REGEX "/GCTests\\.cpp\$")
# core/builtin.cpp и pcre/ucptable.cpp НЕ самостоятельные TU: они включаются
# текстом (core/AbcData.cpp:41 #include "builtin.cpp"; pcre_ucp_findchar.cpp:52
# #include "ucptable.c" — wine regcase находит .cpp). Отдельная компиляция даёт
# «uint32_t/cnode не объявлен».
list(FILTER TAM_CORE EXCLUDE REGEX "/builtin\\.cpp\$")
list(FILTER TAM_PCRE EXCLUDE REGEX "/ucptable\\.cpp\$")
# pcre_ucp_findchar.cpp в этом вендоре НЕ совместим с ucpinternal.h/ucptable.cpp
# (код ждёт cnode с полями f0_*/f1_*/f2_*, заголовок — трёхполевой cnode: дерево
# pcre здесь разношерстное). Вызов _pcre_ucp_findchar в дереве больше нигде не
# встречается — TU мёртвая, исключаем целиком.
list(FILTER TAM_PCRE EXCLUDE REGEX "/pcre_ucp_findchar\\.cpp\$")
# x86-ный inline asm: core/CdeclThunk.cpp (thunk'и вызовов cdecl, VMCFG только для
# IA32) и platform/win32/win32setjmp.cpp. На x64 Tamarin их не использует.
list(FILTER TAM_CORE EXCLUDE REGEX "/CdeclThunk\\.cpp\$")
list(FILTER TAM_PL   EXCLUDE REGEX "/win32setjmp\\.cpp\$")
# VMPI: платформа выбирается на этапе компиляции #ifdef'ами, но Symbian/Mac/Unix
# варианты в win32-сборке не участвуют.
list(FILTER TAM_VMPI EXCLUDE REGEX "(Symbian|Mac|Unix|Posix|WinMo)[A-Za-z]*\\.cpp\$")
list(FILTER TAM_NJ   EXCLUDE REGEX "/Native(ARM|PPC|Sparc|i386)\\.cpp\$")
list(FILTER TAM_PCRE EXCLUDE REGEX "/(pcredemo|pcregrep|pcretest|dftables|pcreposix)\\.cpp\$")
# Vtune.cpp требует vtune-заголовков, coff.cpp — профиль-инструментия; ни то ни
# другое клиентом не вызывается.
list(FILTER TAM_PL   EXCLUDE REGEX "/(Vtune|coff)\\.cpp\$")
# x64: setjmp64/longjmp64/modInternal — masm (win64setjmp.asm); x86-ный
# win32setjmp.cpp под x64 не компилируется (inline asm).
set(TAM_ASM $sw/platform/win32/win64setjmp.asm)
add_library(Tamarin STATIC
  \${TAM_CORE} \${TAM_MMG} \${TAM_VMPI} \${TAM_NJ} \${TAM_PCRE} \${TAM_PL} \${TAM_ASM})
# Машинно-генерированные и «голые» TU (core/builtin.cpp, pcre/*) не имеют ни одного
# #include — в штатной сборке Tamarin они получают заголовки через PCH. Заменяем PCH
# принудительным включением avmplus.h (то же делает клиентский
# Src/UI/Flash/GameSWFIntegration/TamarinPCH.h).
set_source_files_properties(\${TAM_CORE} PROPERTIES COMPILE_OPTIONS "/FIavmplus.h")
# pcre: часть TU (pcre_dfa_exec.cpp) включает config.h только под HAVE_CONFIG_H, а
# pcre_ucp_findchar.cpp — не включает вовсе; без него pcre_internal.h не знает
# LINK_SIZE/NEWLINE (#error C1189). config.h кладём принудительно первым.
set_source_files_properties(\${TAM_PCRE} PROPERTIES COMPILE_OPTIONS "/FIconfig.h;/FIavmplus.h")
target_compile_definitions(Tamarin PRIVATE
  WIN32 _WINDOWS NDEBUG _SHIPPING STATIC_LIB _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
target_compile_options(Tamarin PRIVATE /O2 /MT /EHsc /std:c++14 /wd4996 /wd4244 /wd4267 /wd4018 /wd4099 /wd4311 /wd4312 /wd4715 /wd4100 /wd4101)
EOF
}

# ------------------------------------------------------------------ freetype
# Клиентские заголовки Vendor/freetype/include = freetype 2.4.4 (x86-ная либа —
# freetype244MT.lib, статическая). Исходников в дереве нет — внешний источник:
#   https://downloads.sourceforge.net/freetype/freetype-2.4.4.tar.gz
#   sha256: 3ffd21fe6be219f01be7e41a40a01bbadc8ff6f4f79c4f29cc29dfcf4cc8f5f9
#   каталог: $VSRC/freetype-2.4.4
# Заголовки берём ВЕНДОРСКИЕ (клиентские), а не из архива — раскладки обязаны
# совпадать с тем, что видят TU клиента.
gen_freetype() {
    local out="$V/freetype/lib/x64"
    local f="$VSRC/freetype-2.4.4"
    [ -f "$f/src/base/ftbase.c" ] || { echo "   !! нет источников freetype: $f" >&2; return 1; }
    local inc; inc="$(zfwd "$V/freetype/include")"
    local d
    for d in "$f"/src/*/; do
        case "$(basename "$d")" in tools|dlg) continue ;; esac
        inc="$inc;$(zfwd "$d")"
    done
    cat > "$BUILD/freetype/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(freetype C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
include_directories($inc)
file(GLOB FT_SRC $f/src/*/*.c)
list(FILTER FT_SRC EXCLUDE REGEX "/(ftsystem|ftdebug|ftmac)\\.c\$")
# Модули autofit и gzip — ОДИН TU каждый (autofit.c / ftgzip.c включают
# остальные .c текстом): отдельная компиляция даёт C2129/C2006. CMake-регекспы
# lookahead не умеют, поэтому каталог исключается целиком, а нужный TU
# добавляется явно.
list(FILTER FT_SRC EXCLUDE REGEX "/(autofit|gzip)/")
add_library(freetype244MT STATIC \${FT_SRC} $(zfwd "$f/src/base/ftsystem.c") $(zfwd "$f/src/base/ftdebug.c")
  $(zfwd "$f/src/gzip/ftgzip.c") $(zfwd "$f/src/autofit/autofit.c"))
set_target_properties(freetype244MT PROPERTIES OUTPUT_NAME freetype244MT)
target_compile_definitions(freetype244MT PRIVATE FT2_BUILD_LIBRARY _CRT_SECURE_NO_WARNINGS)
target_compile_options(freetype244MT PRIVATE /O2 /MT /wd4018 /wd4100 /wd4131 /wd4244 /wd4267 /wd4701 /wd4996)
EOF
    DEST="$out" ARTIFACTS="freetype244MT.lib"
}

# ------------------------------------------------------------------ OpenSSL
# Клиентские заголовки Vendor/OpenSSL/include — 0.9.8i, либы — libeay32MT.lib /
# ssleay32MT.lib (статические). Исходников в дереве нет; x64 собирается из
# 1.0.2u: код клиента трогает только публичный API (<openssl/ossl_typ.h>,
# SSL_*/BIO_*/ERR_*), полей структур не видит — ABI-совместимо.
#   внешний источник: https://www.openssl.org/source/old/1.0.2/openssl-1.0.2u.tar.gz
#   sha256: ecd0c6ffb493dd06707d38b14bb4d8c2288bb7033735606569d8f90f89669d16
# Штатный путь (ms\do_win64a + nmake) не годится: util/mk1mf.pl строит пути с
# обратным слэшем и под host-perl не находит источники («no rule for
# crypto\\constant_time_test»), а Windows-perl в префиксе нет. Поэтому — свой
# CMake-список TU (no-asm).
gen_openssl() {
    local out="$V/OpenSSL/lib/x64"
    local o="$VSRC/openssl-1.0.2u"
    [ -f "$o/crypto/cryptlib.c" ] || { echo "   !! нет источников OpenSSL: $o" >&2; return 1; }
    # В tar.gz с GitHub-релиза симлинки include/openssl/*.h ОТСУТСТВУЮТ (в 1.0.2
    # это именно симлинки на crypto/*/*.h) — восстанавливаем их сами, затем
    # генерируем opensslconf.h.
    if [ ! -e "$o/include/openssl/e_os2.h" ]; then
        mkdir -p "$o/include/openssl"
        local h
        for h in "$o"/*.h "$o"/crypto/*.h "$o"/crypto/*/*.h "$o"/ssl/*.h "$o"/engines/*.h; do
            [ -f "$h" ] || continue
            ln -sf "$h" "$o/include/openssl/$(basename "$h")"
        done
    fi
    [ -f "$o/include/openssl/opensslconf.h" ] || (cd "$o" && perl Configure VC-WIN64A no-asm >/dev/null)
    local EAY_DIRS="" d
    for d in "$o"/crypto/*/; do EAY_DIRS="$EAY_DIRS $(zfwd "${d%/}")"; done
    cat > "$BUILD/openssl/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(openssl C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
# каждый crypto/<dir> в include path: внутренние заголовки (asn1_locl.h,
# modes_lcl.h, bn_lcl.h …) включаются по имени из СОСЕДНИХ подкаталогов
foreach(d ${EAY_DIRS})
  include_directories(\${d})
endforeach()
include_directories($(zfwd "$o") $(zfwd "$o/include") $(zfwd "$o/crypto") $(zfwd "$o/ms"))
file(GLOB_RECURSE EAY_SRC $(zfwd "$o")/crypto/*.c)
file(GLOB        SSL_SRC $(zfwd "$o")/ssl/*.c)
# asm-варианты (в т.ч. crypto32/x86), engine (требует динамических движков) и
# fips в no-asm сборке не участвуют
list(FILTER EAY_SRC EXCLUDE REGEX "/(asm|crypto32|engine|fips)/")
# LPdir_* — реализации opendir для разных ОС; для Windows нужна только LPdir_win.c
# LPdir_* (opendir-обвязка; LPdir.h в tar.gz отсутствует), скоростные тесты
# (*speed.c, *_test.c) и bss_rtcp.c (VMS iodef.h) в библиотеку не входят.
list(FILTER EAY_SRC EXCLUDE REGEX "/LPdir_[a-z0-9]*\\.c\$")
list(FILTER EAY_SRC EXCLUDE REGEX "(speed|opts|_spd|test)\\.c\$")
list(FILTER EAY_SRC EXCLUDE REGEX "/(exp|ssl_task)\\.c\$")
list(FILTER EAY_SRC EXCLUDE REGEX "/(bss_rtcp|e_bprint|u_multiss)\\.c\$")
add_library(libeay32MT STATIC \${EAY_SRC})
add_library(ssleay32MT STATIC \${SSL_SRC})
set_target_properties(libeay32MT PROPERTIES OUTPUT_NAME libeay32MT)
set_target_properties(ssleay32MT PROPERTIES OUTPUT_NAME ssleay32MT)
foreach(t libeay32MT ssleay32MT)
  target_compile_definitions(\${t} PRIVATE OPENSSL_NO_ASM OPENSSL_NO_SSL2 OPENSSL_NO_HEARTBEATS _CRT_SECURE_NO_WARNINGS)
  target_compile_options(\${t} PRIVATE /O2 /MT /wd4018 /wd4090 /wd4100 /wd4127 /wd4244 /wd4245 /wd4267 /wd4701 /wd4706 /wd4996 /wd4715 /wd4312)
endforeach()
EOF
    DEST="$out" ARTIFACTS="libeay32MT.lib ssleay32MT.lib"
}

# В список по умолчанию НЕ входят:
#   jpeg     — заголовки клиента (Vendor/jpeglib/include) это САМОДЕЛЬНЫЙ вариант
#            jpeg: jpeg_decompress_struct не совпадает ни с 6b (нет data_unit/
#            J_CODEC_PROCESS/min_codec_data_unit), ни с 8.0 из дерева (есть
#            is_baseline). Ни 6b, ни 8.0 под них не собираются; x64-либу из
#            Vendor/CrashRpt/thirdparty/jpeg (8.0, C-linkage) использовать
#            нельзя — убрана из Vendor/jpeglib/lib/x64. Практичный путь:
#            перевести Src/UI/Flash/GameSWFIntegration/JPEGReader.cpp на стоковый
#            jpeg (8) и собрать его (8 незакрытых символа).
#   crashrpt — требует VC.ATL (atldef.h), в тулчейне ~/.wine-vs его нет.
ALL_TARGETS="zlib jsoncpp ace terabit curl censor tamarin freetype openssl"

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
# ------------------------------------------------------------------ jpeg
# ВАЖНО (ABI): клиентские заголовки Vendor/jpeglib/include — это jpeg **6b**
# (JPEG_LIB_VERSION 62, jconfig.h: HAVE_BOOLEAN + boolean = unsigned char), и
# x86-ный Vendor/jpeglib/lib/jpeglib.lib скомпилирован **как C++** (экспорты
# вида ?jpeg_read_header@@YAHPAUjpeg_decompress_struct@@E@Z). Исходники в
# Vendor/CrashRpt/thirdparty/jpeg — jpeg 8.0 и C-linkage: либа из них НЕ
# совместима (раскладки struct jpeg_decompress_struct разные). Поэтому x64
# собирается из настоящих источников 6b.
#   внешний источник: https://www.ijg.org/files/jpegsrc.v6b.tar.gz
#   sha256: 75c3ec241e9996504fe02a9ed4d12f16b74ade713972f3db9e65ce95cd27e35d
#   каталог: $VSRC/jpeg-6b   (VSRC по умолчанию ~/pwbuild/vendor-src)
gen_jpeg() {
    local out="$V/jpeglib/lib/x64"
    local js="$VSRC/jpeg-6b"
    [ -f "$js/jdapistd.c" ] || { echo "   !! нет источников jpeg 6b: $js (см. комментарий в этом скрипте)" >&2; return 1; }
    # Копируем библиотечные .c в плоский каталог БЕЗ заголовков 6b: quoted
    # #include "jconfig.h"/"jpeglib.h" ищутся в каталоге источника, и иначе
    #sources из архива подхватили бы СВОЙ jconfig.h (не windows) вместо
    # Vendor/jpeglib/include.
    mkdir -p "$BUILD/jpeg/src"
    rm -f "$BUILD/jpeg/src"/*.c
    local f
    for f in "$js"/j*.c; do
        # Заголовки клиента (Vendor/jpeglib/include) помечены «6b 27-Mar-1998», но
        # сторона СЖАТИЯ в них переписана (jpeg_c_codec вместо jpeg_c_coef_controller
        # и т.п.), поэтому из 6b компилируется только декодер + общая обвязка:
        # jc*.c/jdtrans/jquant*/jfdct* под этими заголовками не собираются и
        # клиенту не нужны (в Src/ используется только decompress API).
        case "$(basename "$f")" in
            c*|jc*|jpegtran*|rd*|wr*|jmemdos*|jmemmac*|jmemansi*|jmemname*|jdtrans*|jquant*|jfdct*) continue ;;
        esac
        cp -f "$f" "$BUILD/jpeg/src/"
    done
    cat > "$BUILD/jpeg/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.15)
project(jpeg CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
# заголовки — КЛИЕНТСКИЕ (Vendor/jpeglib/include), не из архива
include_directories($(zfwd "$V/jpeglib/include") $(zfwd "$BUILD/jpeg/src"))
file(GLOB JPEG_SRC $(zfwd "$BUILD/jpeg/src")/*.c)
add_library(jpeglib STATIC \${JPEG_SRC})
set_target_properties(jpeglib PROPERTIES OUTPUT_NAME jpeglib)
# C++ (как x86-ная либа): иначе экспорты без манглинга и клиент их не найдёт
set_source_files_properties(\${JPEG_SRC} PROPERTIES LANGUAGE CXX)
target_compile_definitions(jpeglib PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_compile_options(jpeglib PRIVATE /O2 /MT /EHsc /wd4996 /wd4267 /wd4244 /wd4100 /wd4018 /wd4131 /wd4715 /wd4701 /wd4127)
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
