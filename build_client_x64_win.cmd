@echo off
setlocal enabledelayedexpansion
REM Python output (gen_cmake.py, check_exe.py) is UTF-8; the OEM console codepage
REM mangles it, so switch the console and tell Python explicitly.
chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"
REM NOTE: cmd SHIFT shifts %0 as well, so %~dp0 is only trustworthy BEFORE the
REM argument loop - capture it here (with any argument the repo used to resolve to
REM the current directory; Wine cmd does not shift %0, so this never showed up there).
set "SELF=%~dp0"
REM =============================================================================
REM build_client_x64_win.cmd - native Windows build of ShippingSingleExe x64
REM (PW_Game.exe, cl 14.44, /MT). TWIN of build_client_x64_wine.sh: it calls the
REM SAME generator (Tools/ClientCMake/parse_vcproj.py + gen_cmake.py) with the
REM SAME PW_MACHINE / PW_DEFAULT_OPTS / PW_DROP_LIBS / PW_DROP_NODEFAULT and the
REM SAME cmake arguments. Only what genuinely differs between a native toolchain
REM and Wine differs here:
REM   * compiler environment comes from vcvars64.bat, not from Wine prefix paths;
REM   * paths are native (PW_WIN_ROOT = repo drive letter), no Wine Z: root;
REM   * CMAKE_MSVC_DEBUG_INFORMATION_FORMAT is left EMPTY here as well: if only
REM     one of the two builds passes /Zi, the two builds produce different
REM     binaries and the comparison becomes meaningless. PDB for debugging:
REM     PW_PDB=1 (adds /Zi + /DEBUG, artifact is no longer comparable).
REM   * build dir lives OUTSIDE the repo tree (default C:\pwbuild\x64-win):
REM     1074 PCH wrappers + response files must not hit MAX_PATH, and a CMake
REM     cache generated at one path is invalid at another.
REM
REM Requirements: Python 3 in PATH; Visual Studio 2022 (BuildTools / Community /
REM   ...) with "MSVC v143 - VS 2022 C++ x64/x86 build tools"; Windows 10/11 SDK;
REM   CMake >= 3.15 and ninja (in PATH, or PW_CMAKE / PW_NINJA).
REM
REM Usage (from the repo root):
REM   build_client_x64_win.cmd                      configure + build
REM   build_client_x64_win.cmd --clean              drop gen/ and build dir
REM   build_client_x64_win.cmd --gen-only           generate CMakeLists only
REM   build_client_x64_win.cmd --keep-going         ninja -k0 (survey of errors)
REM   build_client_x64_win.cmd --check              verify existing artifact only
REM   build_client_x64_win.cmd --build-dir D:\pwbuild\x64-win
REM   set PW_BUILD_JOBS=8 & build_client_x64_win.cmd
REM   set PW_EXTRA_OPTS=/std:c++17                  ADDS to toolchain defaults
REM   set PW_BUILD_DIR=C:\pwbuild\x64-win           same as --build-dir
REM
REM Result: <build-dir>\PW_Game.exe. Wine-side reference (2026-10-10):
REM   15 725 568 bytes, machine 0x8664, PE32+, subsystem 2, 0 unresolved.
REM
REM Environment (defaults identical to the Wine script):
REM   PW_DEFAULT_OPTS=/WX- /MT   /MT is mandatory: every x64 vendor .lib in the
REM                              tree is built /MT (the Wine prefix has no
REM                              msvcprt.lib), mixing /MD with them is invalid.
REM                              /WX- because /WX from the vcproj with cl 14.44
REM                              yields ~4.5k errors on new warnings (C4458,
REM                              C4244/C4267 = 64-bit narrowing signals to be
REM                              reviewed one by one, not silenced wholesale).
REM   PW_DROP_LIBS=comsuppw comsuppwd   (removed from VC since VS2017 15.3)
REM   PW_DROP_NODEFAULT=libcmt.lib      vcproj forbids libcmt (it is /MD there),
REM                              a /MT build needs it.
REM   PW_EXTRA_OPTS / PW_EXTRA_DEFS / PW_EXTRA_LINK_OPTS (e.g.
REM                              PW_EXTRA_LINK_OPTS=/MAP for map_rvas.py)
REM   PW_PDB=1                 /Zi + /DEBUG (debug build, NOT comparable to the
REM                              Wine artifact)
REM gen_cmake.py additionally drops/substitutes for x64 (X64_*): sakijapi
REM   (StarForce, no x64 distribution; PSA_* calls are compiled out because
REM   STARFORCE_PROTECTED is not defined), gtrtst32, cxxtest (dead), crashrpt
REM   (needs VC.ATL, code is under #if !defined(_M_X64)); fmod* -> fmod*64,
REM   steam_api -> steam_api64, Lib/x86 -> Lib/x64, redistributable_bin ->
REM   redistributable_bin/win64.
REM
REM Hooks for PLAN_client_build_speedup.md (PCH + object cache):
REM   PW_PCH=<projects|all|off>    consumed by gen_cmake.py, passed by env. ON by
REM     default (=all); PW_PCH=off builds without PCH - do that for release
REM     artifacts, whose bytes must match the baseline (see BUILD_CLIENT_X64.md).
REM   PW_CACHE_LAUNCHER=<sccache/pwcache>  -> -DCMAKE_CXX_COMPILER_LAUNCHER.
REM       Works HERE and not in the Wine script: ninja.exe (Windows) cannot
REM       launch a Linux launcher binary (plan section 2, route A/B).
REM   PW_NINJA_STATS=1               ninja -d stats into the build log
REM Usage notes: --check verifies the artifact without configure/build; use
REM   --gen-only to validate the generated graph only (no compiler is started,
REM   safe to run while another build is running on the same machine).
REM =============================================================================

set "CFG=ShippingSingleExe|Win32"
REM Tag of gen/build dirs and of the generated cmd wrapper. PW_BUILD_TAG keeps
REM several graphs side by side (x64-win = base, x64-win-pch = with PCH) so a
REM comparison run does not wipe the base build dir.
set "TC_TAG=x64-win"
if not "%PW_BUILD_TAG%"=="" set "TC_TAG=%PW_BUILD_TAG%"
set "JOBS=%PW_BUILD_JOBS%"
if "%JOBS%"=="" set "JOBS=8"
set "PW_MACHINE=X64"
if "%PW_DEFAULT_OPTS%"==""   set "PW_DEFAULT_OPTS=/WX- /MT"
if "%PW_DROP_LIBS%"==""      set "PW_DROP_LIBS=comsuppw comsuppwd"
if "%PW_DROP_NODEFAULT%"=="" set "PW_DROP_NODEFAULT=libcmt.lib"

set "DO_CLEAN=0"
set "DO_GEN=0"
set "DO_KEEP=0"
set "DO_CHECK=0"
set "BUILD_DIR=%PW_BUILD_DIR%"
:args
if "%~1"=="" goto argsdone
set "KNOWN=0"
if /i "%~1"=="--clean"      (set "DO_CLEAN=1" & set "KNOWN=1")
if /i "%~1"=="--gen-only"   (set "DO_GEN=1"  & set "KNOWN=1")
if /i "%~1"=="--keep-going" (set "DO_KEEP=1" & set "KNOWN=1")
if /i "%~1"=="--check"      (set "DO_CHECK=1" & set "KNOWN=1")
if /i "%~1"=="--help"       goto usage
if /i "%~1"=="-h"           goto usage
REM cmd treats '=' as an argument separator, so --build-dir=PATH arrives split in
REM two tokens unless quoted: accept both "--build-dir PATH" and quoted "--build-dir=PATH".
if /i "%~1"=="--build-dir" (set "BUILD_DIR=%~2" & set "KNOWN=1" & shift)
REM substring of %~1 is not allowed directly (%~1:~0,11% is a syntax error) - copy first.
set "A1=%~1"
if /i "!A1:~0,12!"=="--build-dir=" (set "BUILD_DIR=!A1:~12!" & set "KNOWN=1")
REM NOTE: "&" in batch separates commands - without parentheses the if binds
REM only to echo, so exit /b would run for ANY argument.
if "%KNOWN%"=="0" (echo == unknown argument: %~1 -- see --help & exit /b 2)
shift
goto args
:argsdone

set "REPO=%SELF%"
for %%i in ("%REPO%") do set "REPO=%%~fi"
set "BR=%REPO%pw\branches\r1117"
set "WCB=%BR%\Tools\WineClientBuild"
set "CCT=%BR%\Tools\ClientCMake"
set "GEN=%CCT%\gen\%TC_TAG%"
if "%BUILD_DIR%"=="" set "BUILD_DIR=C:\pwbuild\%TC_TAG%"
set "EXE=%BUILD_DIR%\PW_Game.exe"

echo == repo: %REPO%
REM CFG contains a pipe (ShippingSingleExe|Win32): plain %CFG% is substituted at
REM parse time and breaks the line as a pipe - use !CFG! (delayed expansion).
echo == target: /MACHINE:X64, configuration: !CFG!
echo == build dir: %BUILD_DIR%

REM ------------------------------------------------------------- 0. python
set "PYTHON="
for %%c in (python py python3) do if "!PYTHON!"=="" (
    for /f "delims=" %%p in ('where %%c 2^>nul') do if "!PYTHON!"=="" set "PYTHON=%%p"
)
if "%PYTHON%"=="" (echo ERROR: python not found in PATH & exit /b 1)
"%PYTHON%" -c "print(1)" >nul 2>nul
if errorlevel 1 (echo ERROR: python does not run: %PYTHON% & exit /b 1)
echo == python: %PYTHON%

REM ------------------------------------------------- 0b. server_ip.h (per-deploy)
REM gitignored, per-deploy: SERVER_IP / ports / token are compiled into the exe.
set "IPH=%BR%\Src\PW_Game\server_ip.h"
if exist "%WCB%\server_ip.env" (
    echo == server values: server_ip.env
    for /f "usebackq eol=# tokens=1,* delims==" %%k in ("%WCB%\server_ip.env") do set "%%k=%%l"
)
if "%PW_SERVER_IP%"==""          set "PW_SERVER_IP=127.0.0.1"
if "%PW_SESSION_TOKEN%"==""      set "PW_SESSION_TOKEN=Tester00Tester00Tester00Tester00"
if "%PW_API_KEY%"==""            set "PW_API_KEY=changeme"
if "%PW_BACKEND_HTTP_PORT%"==""  set "PW_BACKEND_HTTP_PORT=7777"
if "%PW_SERVER_PORT%"==""        set "PW_SERVER_PORT=27300"
if "%PW_LOGIN_PORT%"==""         set "PW_LOGIN_PORT=27301"
if "%PW_CLUSTER_PORT_FRONT%"=="" set "PW_CLUSTER_PORT_FRONT=27310"
if "%PW_CLUSTER_PORT_BACK%"==""  set "PW_CLUSTER_PORT_BACK=27340"
if "%PW_SYNCHRONIZER_PORT%"==""  set "PW_SYNCHRONIZER_PORT=27302"
if not exist "%IPH%" (
    echo == generating Src\PW_Game\server_ip.h ^(SERVER_IP=%PW_SERVER_IP%^)
    "%PYTHON%" "%CCT%\gen_server_ip.py" "%WCB%\server_ip.h.template" "%IPH%"
    if errorlevel 1 (echo ERROR: server_ip.h generation & exit /b 1)
) else (
    echo == server_ip.h already exists - not overwritten
)

REM --------------------------- 0c. compiler environment (vcvars64, not a prefix)
set "VSVARS="
for /d %%v in ("C:\Program Files\Microsoft Visual Studio\2022\*", "C:\Program Files (x86)\Microsoft Visual Studio\2022\*") do (
    if exist "%%v\VC\Auxiliary\Build\vcvars64.bat" set "VSVARS=%%v\VC\Auxiliary\Build\vcvars64.bat"
)
if "%VSVARS%"=="" if not "%VCVARS64%"=="" set "VSVARS=%VCVARS64%"
REM Mode 2: a COPIED x64 toolchain (no VS install, no admin) - PW_MSVC_DIR=...
REM VC\Tools\MSVC\<ver>, PW_KITS_DIR=...\Windows Kits\10; env is built by hand,
REM exactly as build_client_x64_wine.sh does for the Wine prefix. This is how the
REM build runs on tiny10 (the VM has no Visual Studio installed).
if not "%PW_MSVC_DIR%"=="" (
    if not exist "%PW_MSVC_DIR%\bin\HostX64\x64\cl.exe" (
        echo ERROR: PW_MSVC_DIR has no bin\HostX64\x64\cl.exe: %PW_MSVC_DIR%
        exit /b 1
    )
    if "%PW_KITS_DIR%"=="" (echo ERROR: PW_MSVC_DIR set but PW_KITS_DIR is empty & exit /b 1)
    set "KVER="
    REM for /d with a QUOTED pattern does not glob - enumerate via dir instead.
    REM %%KVER%% inside a parenthesized block expands at PARSE time (always empty
    REM here) - the value set by the for below is visible only via delayed expansion.
    for /f "delims=" %%d in ('dir /ad /b "%PW_KITS_DIR%\Include\10.*" 2^>nul') do set "KVER=%%d"
    if "!KVER!"=="" (echo ERROR: no Include\10.* under %PW_KITS_DIR% & exit /b 1)
    set "PATH=%PW_MSVC_DIR%\bin\HostX64\x64;%PW_KITS_DIR%\bin\!KVER!\x64;%PATH%"
    set "INCLUDE=%PW_MSVC_DIR%\include;%PW_KITS_DIR%\Include\!KVER!\ucrt;%PW_KITS_DIR%\Include\!KVER!\um;%PW_KITS_DIR%\Include\!KVER!\shared"
    set "LIB=%PW_MSVC_DIR%\lib\x64;%PW_KITS_DIR%\Lib\!KVER!\ucrt\x64;%PW_KITS_DIR%\Lib\!KVER!\um\x64"
    echo == toolchain: copied MSVC, kits !KVER! ^(PW_MSVC_DIR/PW_KITS_DIR, no vcvars^)
    goto toolchaindone
)
if "%VSVARS%"=="" (
    echo ERROR: vcvars64.bat not found under C:\Program Files [x86]\Microsoft Visual Studio\2022\*
    echo Install VS2022 with "MSVC v143 - VS 2022 C++ x64/x86 build tools" + Windows SDK,
    echo set VCVARS64=path\to\vcvars64.bat, or point PW_MSVC_DIR + PW_KITS_DIR at a
    echo copied toolchain ^(VC\Tools\MSVC\<ver> and Windows Kits\10^).
    exit /b 1
)
echo == vcvars: %VSVARS%
:toolchaindone

REM cmake / ninja: from PATH, or PW_CMAKE / PW_NINJA
set "CMAKE=%PW_CMAKE%"
if "%CMAKE%"=="" set "CMAKE=cmake"
set "NINJA=%PW_NINJA%"
if "%NINJA%"=="" set "NINJA=ninja"

REM PW_EXTRA_OPTS ADDS to the toolchain defaults, it does not replace them:
REM silently losing /WX- and /MT makes the build fail for reasons unrelated to
REM the edit under test (see the vs2022 notes in the pw-client skill).
set "PW_EXTRA_OPTS=%PW_DEFAULT_OPTS% %PW_EXTRA_OPTS%"
REM PCH is ON by default: the gate passed (E2E on tiny10, golden unitstats byte-identical,
REM -43% TU time). pw_rules.py reads an EMPTY PW_PCH as "off", so switching it off must
REM remove the variable instead of writing "off".
if "%PW_PCH%"=="" set "PW_PCH=all"
if /i "%PW_PCH%"=="off" set "PW_PCH="
if /i "%PW_PCH%"=="none" set "PW_PCH="
if /i "%PW_PCH%"=="no" set "PW_PCH="
if /i "%PW_PCH%"=="0" set "PW_PCH="
if "%PW_PCH%"=="" (echo == PCH: OFF ^<release mode^>) else (echo == PCH: %PW_PCH%)
if not "%PW_PCH%"=="" if not "%PW_CACHE_LAUNCHER%"=="" (
    echo !! PCH + cache: sccache marks compiles with /Fp and /Yc as non-cacheable
    echo    ^("Non-cacheable reasons: /Fp, /Yc"^) - the cache will not help here
)
set "DBGFMT="
if "%PW_PDB%"=="1" set "DBGFMT=ProgramDatabase"
echo == extra cl flags: %PW_EXTRA_OPTS%
echo == CMAKE_MSVC_DEBUG_INFORMATION_FORMAT: %DBGFMT%
echo == libs dropped from linking: %PW_DROP_LIBS% (+ generator x64 substitutions)

REM ------------------------------------------------------- 1. model + generation
echo == 1/3. model.json + CMake generation
if "%DO_CLEAN%"=="1" (
    echo == cleaning: %GEN%
    rmdir /s /q "%GEN%" 2>nul
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        echo == cleaning: %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%" 2>nul
    )
)
"%PYTHON%" "%WCB%\parse_vcproj.py" "%BR%\Src" "%CCT%\model.json" "%CFG%" >"%CCT%\parse.out" 2>&1
if errorlevel 1 (
    echo ERROR: parse_vcproj
    type "%CCT%\parse.out"
    exit /b 1
)
REM PW_WIN_ROOT: the root under which the COMPILER sees the tree. In Wine it is
REM Z: (unix root); here it is the repo drive letter, so PCH wrappers get native
REM paths. gen_cmake.py/winpath() reads it.
set "PW_WIN_ROOT=%REPO:~0,2%"
echo == PW_WIN_ROOT=%PW_WIN_ROOT% (PCH wrappers include native paths)
"%PYTHON%" "%CCT%\gen_cmake.py" "%BR%\Src" "%CCT%\model.json" "%GEN%"
if errorlevel 1 exit /b 1
if "%DO_GEN%"=="1" (echo == --gen-only: graph in %GEN% & exit /b 0)

REM ------------------------------------- 2/3. configure + build (native)
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
REM CMakeCache stores absolute gen-/build paths: a mismatch = invalid cache
REM (configure dies with "CMakeCache.txt directory ... is different").
if exist "%BUILD_DIR%\CMakeCache.txt" (
    find /i "CMAKE_HOME_DIRECTORY:INTERNAL=%GEN:\=/%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
    if errorlevel 1 (
        echo == cmake cache was generated for another path - resetting %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%" 2>nul
        mkdir "%BUILD_DIR%"
    )
)
set "KEEP="
if "%DO_KEEP%"=="1" set "KEEP=-k0"
set "LAUNCHER="
if not "%PW_CACHE_LAUNCHER%"=="" set "LAUNCHER=-DCMAKE_CXX_COMPILER_LAUNCHER=%PW_CACHE_LAUNCHER%"
set "NSTATS="
if "%PW_NINJA_STATS%"=="1" set "NSTATS=-d stats"

if "%VSVARS%"=="" goto clbanner
call "%VSVARS%" >"%TEMP%\pwc_vcvars.log" 2>&1
if errorlevel 1 (
    echo ERROR: vcvars64
    type "%TEMP%\pwc_vcvars.log"
    exit /b 1
)
:clbanner
cl >"%TEMP%\pwc_cl.log" 2>&1
findstr /i /c:"Optimizing Compiler Version" "%TEMP%\pwc_cl.log"
del "%TEMP%\pwc_cl.log" "%TEMP%\pwc_vcvars.log" >nul 2>nul

cd /d "%BUILD_DIR%"
REM cmake paths use forward slashes, like the Wine wrapper: in a CMake string a
REM backslash starts an escape sequence, and the generator uses forward slashes (q()).
set "SRC_FWD=%BR%\Src"
set "SRC_FWD=!SRC_FWD:\=/!"
set "R1117_FWD=%BR%"
set "R1117_FWD=!R1117_FWD:\=/!"
set "GEN_FWD=%GEN%"
set "GEN_FWD=!GEN_FWD:\=/!"
if "%DO_CHECK%"=="1" (
    echo == --check: configure and build skipped, verifying artifact only
    goto verify
)
%CMAKE% -G Ninja -DPW_SRC="!SRC_FWD!" -DR1117="!R1117_FWD!" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=%DBGFMT%" ^
  -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO ^
  %LAUNCHER% ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  "!GEN_FWD!"
if errorlevel 1 (echo ERROR: cmake configure & exit /b 1)

echo == 2/3. ninja (-j%JOBS%)
%CMAKE% --build . -- %KEEP% %NSTATS% -j%JOBS%
if errorlevel 1 (echo ERROR: build & exit /b 1)

REM ----------------------------------------------------------- 3. artifact
:verify
if not exist "%EXE%" (
    if "%DO_KEEP%"=="1" (
        echo == 3/3. PW_Game.exe not built ^(--keep-going^): compilation only
        "%PYTHON%" "%CCT%\check_exe.py" "%BUILD_DIR%" --count-obj
        exit /b 0
    )
    echo == 3/3. PW_Game.exe NOT BUILT
    exit /b 1
)
echo == 3/3. %EXE%
"%PYTHON%" "%CCT%\check_exe.py" "%EXE%" "%BR%\Src\Version.h"
exit /b 0

:usage
findstr /b /c:"REM" "%~f0"
exit /b 0
