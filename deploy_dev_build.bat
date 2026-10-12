@echo off
rem Prime World: put a freshly built PW_Game.exe next to the runtime DLLs and start it.
rem   deploy_dev_build.bat                  - ReleaseSingleExe build (with cheats/console)
rem   deploy_dev_build.bat ShippingSingleExe - public build
rem Result folder: pw\branches\r1117\Bin_Dev  (Bin_Original is left untouched)
setlocal
set "BR=%~dp0pw\branches\r1117"
set "CFG=%~1"
if "%CFG%"=="" set "CFG=ReleaseSingleExe"
set "EXE=%BR%\Src\_%CFG%\PW_Game.exe"
set "DST=%BR%\Bin_Dev"

if not exist "%EXE%" (
  echo Built exe not found: %EXE%
  echo Build PW_Game in Visual Studio first, configuration %CFG% ^| Win32.
  pause
  exit /b 1
)

if not exist "%DST%" mkdir "%DST%"

echo Copying runtime files from Bin_Original...
robocopy "%BR%\Bin_Original" "%DST%" /E /XO /XF PW_Game.exe /NJH /NJS /NDL /NFL /NP >nul
if exist "%BR%\Bin\config.txt" copy /Y "%BR%\Bin\config.txt" "%DST%\" >nul

echo Copying %CFG%\PW_Game.exe...
copy /Y "%EXE%" "%DST%\PW_Game.exe" >nul || (echo Copy failed - is the game still running? & pause & exit /b 1)
if exist "%EXE%.manifest" copy /Y "%EXE%.manifest" "%DST%\" >nul

rem Claude can't overwrite files the running game keeps open (main.swf) and leaves *_new.* next to them
set "FLASH=%BR%\Data\UI\Screens\Combat\Flash"
if exist "%FLASH%\main_new.swf" (
  echo Updating combat UI main.swf...
  move /Y "%FLASH%\main_new.swf" "%FLASH%\main.swf" >nul || (echo main.swf is locked - is the game still running? & pause & exit /b 1)
)

if /I "%~2"=="nostart" exit /b 0
echo Starting the game...
start "" /D "%DST%" "%DST%\PW_Game.exe"
