@echo off
rem Prime World: warm up game data in Windows file cache.
rem   warmup_data.bat              - run now
rem   warmup_data.bat -Install     - add to Windows startup
rem   warmup_data.bat -Uninstall   - remove from Windows startup
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warmup_data.ps1" %*
