@echo off
rem Clones of the update repos in UpdateRepos: origin fetches from gitflic and
rem pushes to gitflic + gitlab + github at once. Safe to re-run: existing clones
rem are kept, only missing mirrors and fetch rules are added (no duplicates).
setlocal

if not exist UpdateRepos mkdir UpdateRepos
cd UpdateRepos

rem production launcher (+ testers in refs/testing/test)
call :repo content_pwc https://gitflic.ru/project/prime-world-classic/content_pwc.git https://gitlab.com/prime-world-classic/content.git https://github.com/Prime-World-Classic/content.git testing
rem test launcher (old scheme)
call :repo content-test https://gitflic.ru/project/prime-world-classic/content-test.git https://gitlab.com/prime-world-classic/content-test.git https://github.com/Prime-World-Classic/content-test.git
rem production client (+ testers in refs/testing/test)
call :repo pwc-gitupdates https://gitflic.ru/project/prime-world-classic/pwc-gitupdates.git https://gitlab.com/prime-world-classic/PWCGitUpdates.git https://github.com/Prime-World-Classic/PWCGitUpdates.git testing
rem test client (old scheme)
call :repo pwc-gitupdates-test https://gitflic.ru/project/prime-world-classic/pwc-gitupdates-test.git https://gitlab.com/prime-world-classic/pwc-git-updates-test.git https://github.com/Prime-World-Classic/PWCGitUpdates-test.git

endlocal
exit /b 0

rem :repo <folder> <gitflic> <gitlab> <github> [testing]
:repo
if not exist "%~1\.git" git clone "%~2" "%~1"
call :mirror "%~1" "%~3"
call :mirror "%~1" "%~4"
rem Hidden testers ref (refs/testing/test) is not fetched by default
if "%~5"=="" exit /b 0
git -C "%~1" config --get-all --fixed-value remote.origin.fetch "+refs/testing/*:refs/remotes/origin/testing/*" >nul || git -C "%~1" config --add remote.origin.fetch "+refs/testing/*:refs/remotes/origin/testing/*"
exit /b 0

:mirror
git -C "%~1" config --get-all --fixed-value remote.origin.url "%~2" >nul || git -C "%~1" remote set-url --add origin "%~2"
exit /b 0
