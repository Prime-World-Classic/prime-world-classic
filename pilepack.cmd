@echo off
rem Сборка gamedataNN.pile из Data\ — подробности в PilePack\README.md.
rem Запуск из корня репозитория: pilepack.cmd plan | build | verify | status | extract | clean
cd /d "%~dp0"
where python >nul 2>nul
if %errorlevel%==0 (
    python PilePack\pilepack.py %*
) else (
    py -3 PilePack\pilepack.py %*
)
