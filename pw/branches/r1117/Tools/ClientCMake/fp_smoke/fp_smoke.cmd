@echo off
setlocal enabledelayedexpansion
REM ============================================================================
REM fp_smoke.cmd - bitwise transcendental smoke test, NATIVE Windows build+run.
REM Twin of run_fp_smoke.sh (same source, same cl flags). Run it on every machine
REM whose FP behaviour matters: a real desktop/laptop (FMA3 and non-FMA3 hosts)
REM and the tiny10 VM. Two machines must print the same FP-HASH.
REM
REM Why native matters: under Wine ucrtbase is Wine's implementation, so the Wine
REM golden is only a regression baseline of OUR build. The values players produce
REM come from Microsoft's ucrtbase.dll - only a native run can confirm them.
REM
REM Usage (from Tools\ClientCMake\fp_smoke):
REM   fp_smoke.cmd                      build + run + compare with golden_x64.txt
REM   fp_smoke.cmd --bits               dump every sample to out\*.bits.txt
REM   fp_smoke.cmd --update             rewrite golden_x64.txt from THIS machine
REM                                     (do this on Windows: it is the oracle)
REM   fp_smoke.cmd --no-build           run existing out\*.exe only
REM   set PW_MSVC_DIR=...\VC\Tools\MSVC\<ver>  + PW_KITS_DIR=...\Windows Kits\10
REM       no Visual Studio installed (tiny10 mode, same as build_client_x64_win.cmd)
REM
REM Toolchain: vcvars64.bat (VS2022) or the copied-toolchain mode above.
REM cl flags are the client graph's x64 flags: /O2 /Ob2 /MT /GS- /GF /W4 /WX-
REM (+ /fp:strict for the strict variants).
REM ============================================================================

set "SELF=%~dp0"
for %%i in ("%SELF%..\..\..") do set "BR=%%~fi"
set "OUT=%SELF%out"
set "GOLDEN=%SELF%golden_x64.txt"

set "DO_UPDATE=0"
set "DO_BITS=0"
set "DO_BUILD=1"
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="--update"    (set "DO_UPDATE=1" & shift & goto args)
if /i "%~1"=="--bits"      (set "DO_BITS=1"   & shift & goto args)
if /i "%~1"=="--no-build"  (set "DO_BUILD=0"  & shift & goto args)
if /i "%~1"=="--help"      (findstr /b /c:"REM" "%~f0" & exit /b 0)
echo == unknown argument: %~1
exit /b 2
:argsdone

if not exist "%OUT%" mkdir "%OUT%"

REM ------------------------------------------------------- toolchain environment
if not "%PW_MSVC_DIR%"=="" (
    if not exist "%PW_MSVC_DIR%\bin\HostX64\x64\cl.exe" (
        echo ERROR: PW_MSVC_DIR has no bin\HostX64\x64\cl.exe: %PW_MSVC_DIR%
        exit /b 1
    )
    if "%PW_KITS_DIR%"=="" (echo ERROR: PW_MSVC_DIR set but PW_KITS_DIR is empty & exit /b 1)
    set "KVER="
    for /f "delims=" %%d in ('dir /ad /b "%PW_KITS_DIR%\Include\10.*" 2^>nul') do set "KVER=%%d"
    if "!KVER!"=="" (echo ERROR: no Include\10.* under %PW_KITS_DIR% & exit /b 1)
    set "PATH=%PW_MSVC_DIR%\bin\HostX64\x64;%PW_KITS_DIR%\bin\!KVER!\x64;%PATH%"
    set "INCLUDE=%PW_MSVC_DIR%\include;%PW_KITS_DIR%\Include\!KVER!\ucrt;%PW_KITS_DIR%\Include\!KVER!\um;%PW_KITS_DIR%\Include\!KVER!\shared"
    set "LIB=%PW_MSVC_DIR%\lib\x64;%PW_KITS_DIR%\Lib\!KVER!\ucrt\x64;%PW_KITS_DIR%\Lib\!KVER!\um\x64"
    echo == toolchain: copied MSVC, kits !KVER! ^(PW_MSVC_DIR/PW_KITS_DIR^)
    goto toolchaindone
)
set "VSVARS="
for /d %%v in ("C:\Program Files\Microsoft Visual Studio\2022\*", "C:\Program Files (x86)\Microsoft Visual Studio\2022\*") do (
    if exist "%%v\VC\Auxiliary\Build\vcvars64.bat" set "VSVARS=%%v\VC\Auxiliary\Build\vcvars64.bat"
)
if "%VSVARS%"=="" if not "%VCVARS64%"=="" set "VSVARS=%VCVARS64%"
if "%VSVARS%"=="" (
    echo ERROR: vcvars64.bat not found. Install VS2022 C++ x64 tools, set VCVARS64,
    echo or use PW_MSVC_DIR + PW_KITS_DIR ^(copied toolchain, tiny10 mode^).
    exit /b 1
)
echo == vcvars: %VSVARS%
call "%VSVARS%" >"%TEMP%\fp_smoke_vcvars.log" 2>&1
if errorlevel 1 (echo ERROR: vcvars64 & type "%TEMP%\fp_smoke_vcvars.log" & exit /b 1)
:toolchaindone

REM ------------------------------------------------------------------ 1. build
set "COMMON=/nologo /O2 /Ob2 /MT /GS- /GF /W4 /WX-"
if "%DO_BUILD%"=="1" (
    echo == 1/3. build ^(strict-off / strict-on / precise-off^)
    cd /d "%SELF%"
    cl %COMMON% /fp:strict fp_smoke.cpp /Feout\fp_strict_off.exe /link /SUBSYSTEM:CONSOLE version.lib
    if errorlevel 1 (echo ERROR: build strict-off & exit /b 1)
    cl %COMMON% /fp:strict fp_smoke.cpp /Feout\fp_strict_on.exe  /link /SUBSYSTEM:CONSOLE version.lib
    if errorlevel 1 (echo ERROR: build strict-on & exit /b 1)
    cl %COMMON%           fp_smoke.cpp /Feout\fp_precise_off.exe /link /SUBSYSTEM:CONSOLE version.lib
    if errorlevel 1 (echo ERROR: build precise-off & exit /b 1)
    del /q *.obj >nul 2>nul
)

REM ------------------------------------------------------------------ 2. runs
echo == 2/3. runs
"%OUT%\fp_strict_off.exe"                    >"%OUT%\strict_off.txt"
"%OUT%\fp_strict_off.exe" --fma-default      >"%OUT%\strict_on.txt"
"%OUT%\fp_precise_off.exe"                   >"%OUT%\precise_off.txt"
type "%OUT%\strict_off.txt"
if "%DO_BITS%"=="1" (
    "%OUT%\fp_strict_off.exe" --bits                >"%OUT%\strict_off.bits.txt"
    "%OUT%\fp_strict_off.exe" --bits --fma-default  >"%OUT%\strict_on.bits.txt"
    echo == full dumps: "%OUT%\strict_off.bits.txt" , "%OUT%\strict_on.bits.txt"
)

REM Hash extraction: findstr + for /f inside a subroutine. NEVER put a for with a
REM parenthesised in-set on the same line as an if: cmd closes the if body at the
REM first ')' and the block dies with "set was unexpected at this time" (it bit
REM us once - do not reintroduce it).
call :grabhash "%OUT%\strict_off.txt"   H_OFF
call :grabhash "%OUT%\strict_on.txt"    H_ON
call :grabhash "%OUT%\precise_off.txt"  H_PRE
set "H_GOLD="
if exist "%GOLDEN%" call :grabhash "%GOLDEN%" H_GOLD
set "CPUHASFMA=0"
findstr /b "cpuFMA3=1" "%OUT%\strict_off.txt" >nul 2>nul && set "CPUHASFMA=1"
if "%H_OFF%"=="" (echo ERROR: no FP-HASH in output & exit /b 1)

REM ----------------------------------------------------------------- 3. checks
echo.
echo == 3/3. checks
if "%H_OFF%"=="%H_PRE%" (
    echo OK   /fp:strict does not change the table bits: FP-HASH=%H_OFF%
) else (
    echo FAIL /fp:strict changed the table bits: %H_OFF% vs %H_PRE%
)
if not "%H_OFF%"=="%H_ON%" (
    echo OK   CRT FMA3 dispatch DOES change bits ^(cpu has FMA3 = %CPUHASFMA%^):
    echo        protected   %H_OFF%
    echo        unprotected %H_ON%   =^> _set_FMA3_enable^(0^) is required
) else (
    echo --   FMA3 dispatch made no difference ^(cpu has FMA3 = %CPUHASFMA%^).
    echo      On a non-FMA3 CPU that is expected; on an FMA3 CPU it would mean
    echo      the CRT ignored _set_FMA3_enable^(0^) - investigate.
)

if "%DO_UPDATE%"=="1" (
    call :writegolden "%OUT%\strict_off.txt"
    echo == golden rewritten: %GOLDEN%
    exit /b 0
)
if not "%H_GOLD%"=="" (
    "%OUT%\fp_strict_off.exe" --expect %H_GOLD% >nul
    if errorlevel 1 (
        echo FAIL FP-HASH differs from golden_x64.txt ^(%H_GOLD%^)
        echo      Expected between Wine and Windows ^(different ucrtbase^). To make
        echo      THIS machine the oracle: fp_smoke.cmd --update, then compare machines.
        exit /b 1
    ) else (
        echo OK   FP-HASH matches golden_x64.txt ^(%H_GOLD%^)
    )
) else (
    echo --   no golden_x64.txt here - create with: fp_smoke.cmd --update
)
echo.
echo Client check: the line 'fp: ...' in ^<exe dir^>\fastfail.log must carry the same
echo hash= as FP-HASH above. Different hash = that machine disagrees = desync source.
exit /b 0

REM ------------------------------------------------------------------ helpers
:grabhash
REM %~1 = file, %~2 = name of the variable to set with the FP-HASH value
set "GRAB="
for /f "usebackq tokens=2 delims==" %%v in (`findstr /b "FP-HASH=" %1`) do set "GRAB=%%v"
set "%~2=%GRAB%"
goto :eof

:writegolden
REM %~1 = summary txt of the protected run
(
  echo # golden_x64.txt - bitwise transcendental golden for the x64 client.
  echo # Mode: /fp:strict + _set_FMA3_enable^(0^) - same as the client.
  echo # Update: fp_smoke.cmd --update ^(Windows is the oracle; the Wine runner
  echo # only keeps our own build honest^).
  findstr /b "SAMPLES= FP-HASH= FP-CTRL= FP-DIFF=" %~1
) > "%GOLDEN%"
goto :eof
