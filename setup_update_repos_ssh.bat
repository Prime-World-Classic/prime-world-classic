@echo off
rem Clones of the update repos in UpdateRepos: origin fetches from gitverse and
rem pushes to gitflic + gitlab + github + gitverse at once (game-pwc: no gitflic,
rem it is a retired mirror there). Safe to re-run: existing clones are kept,
rem missing push mirrors are added (no duplicates) and fetch is moved to gitverse.
setlocal

if not exist UpdateRepos mkdir UpdateRepos
cd UpdateRepos

rem launcher content (main/test)
call :repo launcher-pwc git@gitflic.ru:prime-world-classic/launcher-pwc.git git@gitlab.com:prime-world-classic/launcher-pwc.git git@github.com:Prime-World-Classic/launcher-pwc.git git@gitverse.ru:prime_world_classic/launcher-pwc.git
rem game client updates (main/test)
call :repo game-pwc - git@gitlab.com:prime-world-classic/game-pwc.git git@github.com:Prime-World-Classic/game-pwc.git git@gitverse.ru:prime_world_classic/game-pwc.git
call :drop game-pwc git@gitflic.ru:prime-world-classic/game-pwc.git
rem updater in Tools (main/test)
call :repo updater git@gitflic.ru:prime-world-classic/updater.git git@gitlab.com:prime-world-classic/updater.git git@github.com:Prime-World-Classic/updater.git git@gitverse.ru:prime_world_classic/updater.git

endlocal
exit /b 0

rem :repo <folder> <gitflic> <gitlab> <github> <gitverse>
rem All of them go to remote.origin.pushurl (git then pushes only there), fetch url
rem is gitverse; "-" skips a mirror. Push urls are added before the fetch url is
rem replaced, so a clone never loses a mirror on the way.
:repo
if not exist "%~1\.git" git clone "%~5" "%~1"
call :pushurl "%~1" "%~2"
call :pushurl "%~1" "%~3"
call :pushurl "%~1" "%~4"
call :pushurl "%~1" "%~5"
git -C "%~1" config --replace-all remote.origin.url "%~5"
exit /b 0

:pushurl
if "%~2"=="-" exit /b 0
git -C "%~1" config --get-all --fixed-value remote.origin.pushurl "%~2" >nul || git -C "%~1" remote set-url --add --push origin "%~2"
exit /b 0

rem :drop <folder> <url> - remove a retired mirror from the push urls
:drop
git -C "%~1" config --get-all --fixed-value remote.origin.pushurl "%~2" >nul || exit /b 0
git -C "%~1" config --unset --fixed-value remote.origin.pushurl "%~2"
exit /b 0
