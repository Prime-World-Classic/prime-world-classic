@echo off
rem dsm_probe.cmd — сборка и запуск dsm_probe.cpp на tiny10 без vcvars
rem (те же пути, что задаёт build_client_x64_win.cmd: PW_MSVC_DIR / PW_KITS_DIR).
set MSVC=C:\pwbuild\vs2022\VC\Tools\MSVC\14.44.35207
set KITS=C:\pwbuild\vs2022\Windows Kits\10
set PATH=%MSVC%\bin\Hostx64\x64;%PATH%
set INCLUDE=%MSVC%\include;%KITS%\Include\10.0.26100.0\ucrt;%KITS%\Include\10.0.26100.0\um;%KITS%\Include\10.0.26100.0\shared;%KITS%\Include\10.0.26100.0\shared\um
set LIB=%MSVC%\lib\x64;%KITS%\Lib\10.0.26100.0\ucrt\x64;%KITS%\Lib\10.0.26100.0\um\x64
cl /nologo /W3 /EHsc /O2 /Fe:C:\pwbuild\dsm_probe.exe /FoC:\pwbuild\ C:\pwbuild\dsm_probe.cpp /link d3d9.lib user32.lib > C:\pwbuild\dsm_probe_build.log 2>&1
echo RC=%ERRORLEVEL% >> C:\pwbuild\dsm_probe_build.log
C:\pwbuild\dsm_probe.exe > C:\pwbuild\dsm_probe.log 2>&1
echo PROBE_RC=%ERRORLEVEL% >> C:\pwbuild\dsm_probe.log
