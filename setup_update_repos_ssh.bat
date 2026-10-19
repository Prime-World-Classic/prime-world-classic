@echo off
rem Clones of the update repos in UpdateRepos: origin fetches from gitflic and
rem pushes to gitflic + gitlab + github + gitverse at once. Safe to re-run: existing
rem clones are kept, only missing mirrors and fetch rules are added (no duplicates).
setlocal

if not exist UpdateRepos mkdir UpdateRepos
cd UpdateRepos

rem launcher content (main/test)
call :repo launcher-pwc git@gitflic.ru:prime-world-classic/launcher-pwc.git git@gitlab.com:prime-world-classic/launcher-pwc.git git@github.com:Prime-World-Classic/launcher-pwc.git git@gitverse.ru:prime_world_classic/launcher-pwc.git
rem game client updates (main/test)
call :repo game-pwc git@gitflic.ru:prime-world-classic/game-pwc.git git@gitlab.com:prime-world-classic/game-pwc.git git@github.com:Prime-World-Classic/game-pwc.git git@gitverse.ru:prime_world_classic/game-pwc.git
rem updater in Tools (main/test)
call :repo updater git@gitflic.ru:prime-world-classic/updater.git git@gitlab.com:prime-world-classic/updater.git git@github.com:Prime-World-Classic/updater.git git@gitverse.ru:prime_world_classic/updater.git

endlocal
exit /b 0

rem :repo <folder> <gitflic> <gitlab> <github> [gitverse]
:repo
if not exist "%~1\.git" git clone "%~2" "%~1"
call :mirror "%~1" "%~3"
call :mirror "%~1" "%~4"
call :mirror "%~1" "%~5"
exit /b 0

:mirror
if "%~2"=="" exit /b 0
git -C "%~1" config --get-all --fixed-value remote.origin.url "%~2" >nul || git -C "%~1" remote set-url --add origin "%~2"
rem Clones with explicit pushurl entries push only to pushurl, so the mirror is
rem added as pushurl too when the clone has any.
git -C "%~1" config --get-all remote.origin.pushurl >nul 2>nul || exit /b 0
git -C "%~1" config --get-all --fixed-value remote.origin.pushurl "%~2" >nul || git -C "%~1" remote set-url --add --push origin "%~2"
exit /b 0
