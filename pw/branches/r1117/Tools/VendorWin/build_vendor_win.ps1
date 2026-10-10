<#
.SYNOPSIS
  Пересборка вендорных C++-библиотек клиента под x86 новым MSVC (VS2022, v143) на Windows.

.DESCRIPTION
  x86-ные .lib в Vendor/ собраны VC90. C-библиотеки с import-либой (zlib1.dll, fmod, d3dx9) линкуются
  и новым компилятором, а статические библиотеки, которые используют CRT, — нет:
    * JsonCpp — C++ (std::string VC90 несовместим с новым STL);
    * libcurl, Tamarin — используют stdio старого CRT (__iob_func, импорт vsnprintf/vsprintf);
      заглушка __iob_func тут не годится: старый код индексирует FILE с размером VC90 (32 байта).
  Рецепты (списки TU, define'ы) — те же, что у x64 в Tools/VendorX64/build_vendor_x64.sh (Wine);
  отличия x86 помечены в коде. CRT — /MD, как в vcproj клиента.

  Результат кладётся рядом с VC90-либой в подкаталог x86-v143 (VS2008-сборка продолжает брать свою):
    Vendor/JsonCpp/lib/Release/x86-v143/JsonCpp.lib
    Vendor/libcurl/lib/Release/x86-v143/libcurl.lib
    Vendor/Tamarin/lib/Release/x86-v143/Tamarin.lib
    Vendor/OpenSSL/lib/static/x86-v143/openssl_crt_compat.lib  (заглушка __iob_func для VC90 OpenSSL)
  gen_cmake.py подставляет эти каталоги при PW_MACHINE=X86 и PW_X86_VENDOR_SUBDIR=x86-v143.

.PARAMETER Targets  Что собирать: jsoncpp, curl, tamarin (по умолчанию все).
.PARAMETER OutRoot  Каталог сборки (вне репозитория).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File pw\branches\r1117\Tools\VendorWin\build_vendor_win.ps1
#>
param(
  [ValidateSet('jsoncpp', 'curl', 'tamarin', 'crtcompat')][string[]]$Targets = @('jsoncpp', 'curl', 'tamarin', 'crtcompat'),
  [string]$OutRoot = (Join-Path $env:USERPROFILE 'pwbuild\VendorWin'),
  [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
$r1117 = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$V = Join-Path $r1117 'Vendor'
$sub = 'x86-v143'
function Fwd([string]$p) { return $p -replace '\\', '/' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Не найдена Visual Studio 2022 с компонентом MSVC x86/x64' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

$recipes = @{}

# ---------------------------------------------------------------------------------- JsonCpp
$js = Fwd (Join-Path $V 'JsonCpp')
$recipes['jsoncpp'] = @{
  Dest = Join-Path $V "JsonCpp\lib\Release\$sub"; Artifact = 'JsonCpp.lib'
  CMake = @"
cmake_minimum_required(VERSION 3.15)
project(jsoncpp CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
include_directories($js/include $js/src)
add_library(JsonCpp STATIC $js/src/lib_json/json_reader.cpp $js/src/lib_json/json_value.cpp $js/src/lib_json/json_writer.cpp)
set_target_properties(JsonCpp PROPERTIES OUTPUT_NAME JsonCpp)
target_compile_definitions(JsonCpp PRIVATE JSON_DLL_STATIC WIN32 NDEBUG _CRT_SECURE_NO_WARNINGS)
target_compile_options(JsonCpp PRIVATE /O2 /MD /GS- /GF /EHsc /std:c++14 /wd4996 /wd4267 /wd4244 /wd4100 /wd4251 /wd4275)
"@
}

# ---------------------------------------------------------------------------------- libcurl
# curl 7.22.0 из дерева; x86-ная VC90-либа тоже без SSL (в ней нет SSL_*), так что поведение то же.
$c = Fwd (Join-Path $V 'libcurl\src\curl-7.22.0')
$recipes['curl'] = @{
  Dest = Join-Path $V "libcurl\lib\Release\$sub"; Artifact = 'libcurl.lib'
  CMake = @"
cmake_minimum_required(VERSION 3.15)
project(curl C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
include_directories($c/include $c/lib)
file(GLOB CURL_SRC $c/lib/*.c)
list(FILTER CURL_SRC EXCLUDE REGEX "/(gssapi|spnego|darwinssl)\\.c$")
add_library(libcurl STATIC `${CURL_SRC})
set_target_properties(libcurl PROPERTIES OUTPUT_NAME libcurl)
target_compile_definitions(libcurl PRIVATE BUILDING_LIBCURL CURL_STATICLIB CURL_DISABLE_LDAP _CRT_SECURE_NO_WARNINGS)
target_compile_options(libcurl PRIVATE /O2 /MD /wd4018 /wd4090 /wd4100 /wd4127 /wd4244 /wd4245 /wd4267 /wd4701 /wd4996)
"@
}

# ---------------------------------------------------------------------------------- Tamarin
# Как x64-рецепт, но x86: остаётся core/CdeclThunk.cpp (x86 inline asm), бэкенд nanojit —
# Nativei386.cpp, masm-файла win64setjmp.asm нет. platform/win32/win32setjmp.cpp (подмена CRT-шных
# _setjmp3/longjmp) исключён: в оригинальной VC90 Tamarin.lib его объекта нет — она берёт их из CRT.
$s = Fwd (Join-Path $V 'Tamarin\source')
$tv = Fwd (Join-Path $V 'Tamarin')
$recipes['tamarin'] = @{
  Dest = Join-Path $V "Tamarin\lib\Release\$sub"; Artifact = 'Tamarin.lib'
  CMake = @"
cmake_minimum_required(VERSION 3.15)
project(tamarin C CXX)
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
include_directories($s/core $s/MMgc $s/VMPI $s/nanojit $s/pcre $s/platform $s/eval $s/extensions $s/shell $s/vprof $tv)
file(GLOB TAM_CORE $s/core/*.cpp)
file(GLOB TAM_MMG  $s/MMgc/*.cpp)
file(GLOB TAM_VMPI $s/VMPI/*.cpp)
file(GLOB TAM_NJ   $s/nanojit/*.cpp)
file(GLOB TAM_PCRE $s/pcre/*.cpp)
file(GLOB TAM_PL   $s/platform/win32/*.cpp)
list(FILTER TAM_MMG  EXCLUDE REGEX "/GCTests\\.cpp$")
list(FILTER TAM_CORE EXCLUDE REGEX "/builtin\\.cpp$")
list(FILTER TAM_PCRE EXCLUDE REGEX "/(ucptable|pcre_ucp_findchar|pcredemo|pcregrep|pcretest|dftables|pcreposix)\\.cpp$")
list(FILTER TAM_VMPI EXCLUDE REGEX "(Symbian|Mac|Unix|Posix|WinMo)[A-Za-z]*\\.cpp$")
list(FILTER TAM_NJ   EXCLUDE REGEX "/Native(ARM|PPC|Sparc|X64)\\.cpp$")
list(FILTER TAM_PL   EXCLUDE REGEX "/(Vtune|coff|win32setjmp)\\.cpp$")
add_library(Tamarin STATIC `${TAM_CORE} `${TAM_MMG} `${TAM_VMPI} `${TAM_NJ} `${TAM_PCRE} `${TAM_PL})
set_source_files_properties(`${TAM_CORE} PROPERTIES COMPILE_OPTIONS "/FIavmplus.h")
set_source_files_properties(`${TAM_PCRE} PROPERTIES COMPILE_OPTIONS "/FIconfig.h;/FIavmplus.h")
target_compile_definitions(Tamarin PRIVATE WIN32 _WINDOWS NDEBUG _SHIPPING STATIC_LIB _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
target_compile_options(Tamarin PRIVATE /O2 /MD /EHsc /std:c++14 /wd4996 /wd4244 /wd4267 /wd4018 /wd4099 /wd4715 /wd4100 /wd4101)
"@
}

# ---------------------------------------------------------------------------- crtcompat
# __iob_func для VC90-шного OpenSSL (исходников OpenSSL в дереве нет) — см. openssl_crt_compat.c.
$cc = Fwd (Join-Path $PSScriptRoot 'openssl_crt_compat.c')
$recipes['crtcompat'] = @{
  Dest = Join-Path $V "OpenSSL\lib\static\$sub"; Artifact = 'openssl_crt_compat.lib'
  CMake = @"
cmake_minimum_required(VERSION 3.15)
project(crtcompat C)
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_RELEASE "")
add_library(openssl_crt_compat STATIC $cc)
target_compile_options(openssl_crt_compat PRIVATE /O2 /MD /utf-8)
"@
}

$fail = $false
foreach ($t in $Targets) {
  $r = $recipes[$t]
  $bdir = Join-Path $OutRoot "x86\$t"
  New-Item -ItemType Directory -Force $bdir | Out-Null
  Set-Content -Path (Join-Path $bdir 'CMakeLists.txt') -Value $r.CMake -Encoding ascii
  $cmd = Join-Path $bdir 'build.cmd'
  @"
@echo off
call "$vcvars" x86 >nul || exit /b 1
cd /d "$bdir"
"$cmake" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" "-DCMAKE_MAKE_PROGRAM=$(Fwd $ninja)" -S . -B out || exit /b 1
"$cmake" --build out -- -j$Jobs
"@ | Set-Content -Path $cmd -Encoding ascii
  Write-Host "== $t"
  $log = Join-Path $bdir 'build.log'
  & cmd /c "`"$cmd`" > `"$log`" 2>&1"
  $art = Get-ChildItem (Join-Path $bdir 'out') -Recurse -Filter $r.Artifact -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($LASTEXITCODE -ne 0 -or -not $art) {
    Select-String -Path $log -Pattern ' error ' | Select-Object -First 15 | ForEach-Object { Write-Host "   $($_.Line)" }
    Write-Host "   !! $t не собран, лог: $log"
    $fail = $true
    continue
  }
  New-Item -ItemType Directory -Force $r.Dest | Out-Null
  Copy-Item $art.FullName $r.Dest -Force
  Write-Host ("   -> {0} ({1} байт)" -f (Join-Path $r.Dest $r.Artifact), $art.Length)
}
if ($fail) { exit 1 }
