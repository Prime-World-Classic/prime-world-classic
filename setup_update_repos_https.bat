@echo off
rem Clones of the update repos in UpdateRepos: origin fetches from gitverse and
rem pushes to gitlab + github + gitverse at once. gitflic is retired for all three
rem repos (it does not accept big packs), so it is listed nowhere and is dropped
rem from clones that still have it. Safe to re-run: existing clones are kept,
rem missing push mirrors are added (no duplicates) and fetch is moved to gitverse.
setlocal

if not exist UpdateRepos mkdir UpdateRepos
cd UpdateRepos

rem launcher content (main/test)
call :repo launcher-pwc https://gitlab.com/prime-world-classic/launcher-pwc.git https://github.com/Prime-World-Classic/launcher-pwc.git https://gitverse.ru/prime_world_classic/launcher-pwc.git
call :drop launcher-pwc https://gitflic.ru/project/prime-world-classic/launcher-pwc.git
rem game client updates (main/test)
call :repo game-pwc https://gitlab.com/prime-world-classic/game-pwc.git https://github.com/Prime-World-Classic/game-pwc.git https://gitverse.ru/prime_world_classic/game-pwc.git
call :drop game-pwc https://gitflic.ru/project/prime-world-classic/game-pwc.git
rem updater in Tools (main/test)
call :repo updater https://gitlab.com/prime-world-classic/updater.git https://github.com/Prime-World-Classic/updater.git https://gitverse.ru/prime_world_classic/updater.git
call :drop updater https://gitflic.ru/project/prime-world-classic/updater.git

endlocal
exit /b 0

rem :repo <folder> <gitlab> <github> <gitverse>
rem All of them go to remote.origin.pushurl (git then pushes only there), fetch url
rem is gitverse; "-" skips a mirror. Push urls are added before the fetch url is
rem replaced, so a clone never loses a mirror on the way.
:repo
if not exist "%~1\.git" git clone "%~4" "%~1"
call :pushurl "%~1" "%~2"
call :pushurl "%~1" "%~3"
call :pushurl "%~1" "%~4"
git -C "%~1" config --replace-all remote.origin.url "%~4"
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
