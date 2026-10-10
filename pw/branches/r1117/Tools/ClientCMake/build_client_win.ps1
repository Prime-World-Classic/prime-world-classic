<#
.SYNOPSIS
  Нативная Windows-сборка клиента (PW_Game.exe) через CMake-граф gen_cmake.py — аналог
  build_client_cmake.sh без Wine.

.DESCRIPTION
  Граф тот же: parse_vcproj.py -> model.json -> gen_cmake.py -> CMake (Ninja). Отличия от Wine-пути:
    * компилятор — установленная Visual Studio 2022 (окружение из vcvarsall.bat), cmake/ninja — встроенные в VS;
    * пути — обычные Windows-пути (PW_WIN_ROOT пустой, без Z:).
  Сборка запускается из PowerShell: Git Bash (MSYS) превращает флаги вида /WX- в пути.

  Каталоги gen\ и build\ — вне репозитория (-OutRoot, по умолчанию %USERPROFILE%\pwbuild\ClientCMake):
  репозиторий часто лежит в OneDrive, а сборка — это гигабайты .obj.
  Src\PW_Game\server_ip.h должен существовать (per-deploy файл, не в git; шаблон —
  Tools\WineClientBuild\server_ip.h.template).

.PARAMETER Arch       x64 (по умолчанию) или x86.
.PARAMETER Clean      Удалить gen- и build-каталоги перед сборкой.
.PARAMETER GenOnly    Только сгенерировать CMake-проект.
.PARAMETER KeepGoing  ninja -k0: не останавливаться на первой ошибке (обзор ошибок компиляции).
.PARAMETER Jobs       Число параллельных задач ninja (по умолчанию — число логических ядер).
.PARAMETER OutRoot    Куда класть gen\ и build\ (по умолчанию %USERPROFILE%\pwbuild\ClientCMake).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File pw\branches\r1117\Tools\ClientCMake\build_client_win.ps1 -Arch x64
#>
param(
  [ValidateSet('x64', 'x86')][string]$Arch = 'x64',
  [switch]$Clean,
  [switch]$GenOnly,
  [switch]$KeepGoing,
  [int]$Jobs = [Environment]::ProcessorCount,
  [string]$OutRoot = (Join-Path $env:USERPROFILE 'pwbuild\ClientCMake')
)

$ErrorActionPreference = 'Stop'
$r1117 = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$srcDir = Join-Path $r1117 'Src'
$cct = $PSScriptRoot
$tag = "win-vs2022-$Arch"
$genDir = Join-Path $OutRoot "gen\$tag"
$buildDir = Join-Path $OutRoot "build\$tag"

if (-not (Test-Path (Join-Path $srcDir 'PW_Game\server_ip.h'))) {
  throw 'Нет Src\PW_Game\server_ip.h — создайте из Tools\WineClientBuild\server_ip.h.template (файл не коммитится)'
}

# --- Visual Studio 2022: vcvarsall, cmake, ninja --------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Не найдена Visual Studio 2022 с компонентом MSVC x86/x64' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
foreach ($f in $vcvars, $cmake, $ninja) { if (-not (Test-Path $f)) { throw "Не найден $f (компонент «Средства CMake C++ для Windows»)" } }

# --- настройки графа (читает gen_cmake.py) ---------------------------------------------------
# пустое значение в PowerShell 5.1 не задать ($env:X = '' удаляет переменную) — gen_cmake.py
# на Windows сам берёт пустой префикс, здесь только убираем возможный унаследованный Z:
Remove-Item Env:PW_WIN_ROOT -ErrorAction SilentlyContinue
$env:PW_CFG = 'ShippingSingleExe|Win32'
if ($Arch -eq 'x64') {
  # как vs2022 в build_client_cmake.sh: /MT (вендорные x64-либы собраны /MT), comsuppw удалён из VC
  $env:PW_MACHINE = 'X64'
  if (-not $env:PW_EXTRA_OPTS) { $env:PW_EXTRA_OPTS = '/WX- /MT' }
  if ($null -eq $env:PW_DROP_LIBS) { $env:PW_DROP_LIBS = 'comsuppw comsuppwd' }
  if ($null -eq $env:PW_DROP_NODEFAULT) { $env:PW_DROP_NODEFAULT = 'libcmt.lib' }
  $vcArch = 'x64'
} else {
  # x86 (этап A ТЗ): /arch:IA32 — x87, как у VS2008; без него cl 14 генерирует SSE2,
  # и lockstep-логика считает иначе, чем у старых клиентов.
  $env:PW_MACHINE = 'X86'
  # /Zc:threadSafeInit-: в VS2008 локальные static инициализировались без блокировок; этап A
  # повторяет поведение старого клиента.
  if (-not $env:PW_EXTRA_OPTS) { $env:PW_EXTRA_OPTS = '/WX- /arch:IA32 /Zc:threadSafeInit-' }
  if ($null -eq $env:PW_DROP_LIBS) { $env:PW_DROP_LIBS = 'comsuppw comsuppwd' }
  if ($null -eq $env:PW_DROP_NODEFAULT) { $env:PW_DROP_NODEFAULT = '' }
  # VC90-ные C++-либы вендора пересобраны v143 в <dir>\x86-v143 (Tools\VendorWin\build_vendor_win.ps1);
  # оставшимся VC90 C-либам (OpenSSL) нужны legacy stdio и заглушка __iob_func.
  if (-not $env:PW_X86_VENDOR_SUBDIR) { $env:PW_X86_VENDOR_SUBDIR = 'x86-v143' }
  if (-not $env:PW_EXTRA_LINK_LIBS) { $env:PW_EXTRA_LINK_LIBS = 'legacy_stdio_definitions.lib openssl_crt_compat.lib' }
  $vcArch = 'x86'
}
Write-Host "== мишень $env:PW_MACHINE, доп. флаги cl: $env:PW_EXTRA_OPTS"

if ($Clean) { foreach ($d in $genDir, $buildDir) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } } }

# --- 1. модель + генерация ---------------------------------------------------------------------
Write-Host '== 1/3. model.json + генерация CMake'
New-Item -ItemType Directory -Force $OutRoot | Out-Null
$model = Join-Path $OutRoot "model-$tag.json"
& py (Join-Path $r1117 'Tools\WineClientBuild\parse_vcproj.py') $srcDir $model $env:PW_CFG | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'parse_vcproj.py завершился с ошибкой' }
& py (Join-Path $cct 'gen_cmake.py') $srcDir $model $genDir
if ($LASTEXITCODE -ne 0) { throw 'gen_cmake.py завершился с ошибкой' }
if ($GenOnly) { return }

# --- 2. configure + build (окружение vcvarsall в одном cmd-процессе) -------------------------
New-Item -ItemType Directory -Force $buildDir | Out-Null
$fwd = { param($p) $p -replace '\\', '/' }
$keep = if ($KeepGoing) { '-k0' } else { '' }
$cmd = Join-Path $buildDir 'build_env.cmd'
@"
@echo off
call "$vcvars" $vcArch >nul || exit /b 1
cd /d "$buildDir"
"$cmake" -G Ninja "-DPW_SRC=$(& $fwd $srcDir)" "-DR1117=$(& $fwd $r1117)" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release ^
  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=" ^
  -DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:NO ^
  "-DCMAKE_MAKE_PROGRAM=$(& $fwd $ninja)" ^
  "$(& $fwd $genDir)" || exit /b 1
"$cmake" --build . -- $keep -j$Jobs
"@ | Set-Content -Path $cmd -Encoding ascii

Write-Host "== 2/3. cmake -G Ninja + сборка ($buildDir)"
$log = Join-Path $buildDir 'build.log'
& cmd /c "`"$cmd`" > `"$log`" 2>&1"
$rc = $LASTEXITCODE

# --- 3. результат ------------------------------------------------------------------------------
$errs = Select-String -Path $log -Pattern ' error [A-Z]+\d+' -AllMatches
if ($errs) {
  Write-Host "== ошибок в логе: $($errs.Count); по кодам:"
  $errs | ForEach-Object { $_.Matches } | ForEach-Object { $_.Value.Trim() } | Group-Object | Sort-Object Count -Descending |
    Select-Object -First 15 | ForEach-Object { '   {0,6}  {1}' -f $_.Count, $_.Name }
}
$exe = Join-Path $buildDir 'PW_Game.exe'
if (Test-Path $exe) {
  $bytes = [IO.File]::ReadAllBytes($exe)
  $pe = [BitConverter]::ToInt32($bytes, 0x3c)
  $mach = [BitConverter]::ToUInt16($bytes, $pe + 4)
  $chars = [BitConverter]::ToUInt16($bytes, $pe + 22)
  Write-Host ("== 3/3. PW_Game.exe: {0} байт, machine=0x{1:x4}, LAA={2}" -f $bytes.Length, $mach, [bool]($chars -band 0x20))
} else {
  Write-Host "== 3/3. PW_Game.exe НЕ собран (rc=$rc), лог: $log"
  if (-not $KeepGoing) { exit 1 }
}
