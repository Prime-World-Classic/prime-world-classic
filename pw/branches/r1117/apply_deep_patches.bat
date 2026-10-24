@echo off
rem Copies files that are too deep for Claudes file bridge (more than 7 folders below D:\PrimeGit\Prime-World)
rem from _deep_patches\ to their real place in the source tree. Run once after Claude puts files there.
setlocal
set "R=%~dp0"
set "P=%R%_deep_patches"
if exist "%P%\TextField.cpp" copy /Y "%P%\TextField.cpp" "%R%Src\UI\Flash\GameSWFIntegration\Natives\text\TextField.cpp" && del "%P%\TextField.cpp"
echo Done.
