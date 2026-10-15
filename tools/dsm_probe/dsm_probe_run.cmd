@echo off
C:\pwbuild\dsm_probe.exe > C:\pwbuild\dsm_probe.log 2>&1
echo PROBE_RC=%ERRORLEVEL% >> C:\pwbuild\dsm_probe.log
