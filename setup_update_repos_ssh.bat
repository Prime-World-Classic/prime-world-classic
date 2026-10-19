@echo off
rem Clones of the update repos in UpdateRepos: origin fetches from gitflic and
rem pushes to gitflic + gitlab + github at once. Safe to re-run: existing clones
rem are kept, only missing mirrors and fetch rules are added (no duplicates).
setlocal

if not exist UpdateRepos mkdir UpdateRepos
cd UpdateRepos

rem launcher content (main/test)
call :repo launcher-pwc git@gitflic.ru:prime-world-classic/launcher-pwc.git git@gitlab.com:prime-world-classic/launcher-pwc.git git@github.com:Prime-World-Classic/launcher-pwc.git
rem game client updates (main/test)
call :repo game-pwc git@gitflic.ru:prime-world-classic/game-pwc.git git@gitlab.com:prime-world-classic/game-pwc.git git@github.com:Prime-World-Classic/game-pwc.git
rem updater in Tools (main/test)
call :repo updater git@gitflic.ru:prime-world-classic/updater.git git@gitlab.com:prime-world-classic/updater.git git@github.com:Prime-World-Classic/updater.git

endlocal
exit /b 0

rem :repo <folder> <gitflic> <gitlab> <github>
:repo
if not exist "%~1\.git" git clone "%~2" "%~1"
call :mirror "%~1" "%~3"
call :mirror "%~1" "%~4"
exit /b 0

:mirror
git -C "%~1" config --get-all --fixed-value remote.origin.url "%~2" >nul || git -C "%~1" remote set-url --add origin "%~2"
exit /b 0
