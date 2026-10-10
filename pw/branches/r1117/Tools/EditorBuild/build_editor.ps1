<#
.SYNOPSIS
  Сборка редактора PF_Editor (C#-часть) в Visual Studio 2022 / .NET Framework 4.8, x86,
  и раскладка рабочей папки _EditorRun.

.DESCRIPTION
  Этап E1a (docs/x64/EDITOR.md): C#-проекты собираются новым MSBuild, нативные C++/CLI-сборки
  (EditorNative, PF_EditorNative, FormulaBuilder) и DLL движка берутся готовыми из Bin_Original.

  Результат:
    r1117\_EditorRun\Bin       — Bin_Original + свежесобранные управляемые сборки
    r1117\_EditorRun\Profiles  — копия r1117\Profiles (создаётся один раз, дальше не трогается),
                                 в PF_Editor.config прописан корень данных (-DataRoot)
  Папка _EditorRun в .gitignore: отслеживаемый r1117\Profiles редактор не меняет.

.PARAMETER DataRoot   Корень базы данных (по умолчанию r1117\Data).
.PARAMETER CodeGen    Не отключать pre/post-build шаги DBCodeGen (перегенерация *.DBTypes и C++-кода).
.PARAMETER Run        После сборки запустить редактор.
.PARAMETER NoBuild    Только разложить _EditorRun из уже собранного Src\_Release.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Tools\EditorBuild\build_editor.ps1 -Run
#>
param(
  [string]$DataRoot = '',
  [switch]$CodeGen,
  [switch]$Run,
  [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$r1117 = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$src = Join-Path $r1117 'Src'
$out = Join-Path $src '_Release'
$runDir = Join-Path $r1117 '_EditorRun'
$bin = Join-Path $runDir 'Bin'
$profiles = Join-Path $runDir 'Profiles'
if (-not $DataRoot) { $DataRoot = Join-Path $r1117 'Data' }

# Проекты верхнего уровня; остальные C#-проекты подтягиваются через ProjectReference.
$projects = @(
  'PF_Editor\PF_Editor.csproj',
  'PF_EditorC\PF_EditorC.csproj',
  'EaselLevelEditor\EaselLevelEditor.csproj',
  'DBCodeGen\DBCodeGen.csproj',
  'PF_TypeIcons\PF_TypeIcons.csproj',
  'PF_Types\PF_Types.csproj',
  'SocialTypes\SocialTypes.csproj'
)
# Управляемые сборки, которые кладутся поверх Bin_Original.
$assemblies = @(
  'libdb.NET.dll', 'Win32.dll', 'Types.dll', 'PF_Types.dll', 'SocialTypes.dll',
  'Types.DBTypes.dll', 'PF_Types.DBTypes.dll', 'SocialTypes.DBTypes.dll',
  'EditorLib.dll', 'EditorPlugins.dll', 'PF_TypeIcons.dll', 'MayaExeInteraction.dll',
  'PF_Editor.exe', 'PF_Editor.exe.config', 'PF_EditorC.exe', 'PF_EditorC.exe.config',
  'EaselLevelEditor.exe', 'DBCodeGen.exe'
)

function Find-MSBuild {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $vswhere)) { throw 'vswhere.exe не найден — установите Visual Studio 2022' }
  $path = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
  if (-not $path) { throw 'MSBuild не найден' }
  return $path
}

if (-not $NoBuild) {
  $msbuild = Find-MSBuild
  $logDir = Join-Path $runDir 'logs'
  New-Item -ItemType Directory -Force $logDir | Out-Null
  foreach ($p in $projects) {
    $name = [IO.Path]::GetFileNameWithoutExtension($p)
    $log = Join-Path $logDir "build_$name.log"
    $msbArgs = @((Join-Path $src $p), '-nologo', '-m', '-v:minimal',
              '-p:Configuration=Release', '-p:Platform=Win32',
              "-flp:logfile=$log;verbosity=normal")
    if (-not $CodeGen) { $msbArgs += @('-p:PreBuildEvent=', '-p:PostBuildEvent=') }
    Write-Host "== $name"
    & $msbuild @msbArgs | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Select-String -Path $log -Pattern ': error ' | Select-Object -First 20 | ForEach-Object { Write-Host $_.Line }
      throw "Сборка $name завершилась с ошибкой (лог: $log)"
    }
  }
}

# Раскладка Bin: Bin_Original целиком, затем свежие управляемые сборки поверх.
New-Item -ItemType Directory -Force $bin | Out-Null
& robocopy (Join-Path $r1117 'Bin_Original') $bin /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy Bin_Original -> ${bin}: код $LASTEXITCODE" }
foreach ($a in $assemblies) {
  $f = Join-Path $out $a
  if (-not (Test-Path $f)) { throw "Нет $f — сначала соберите без -NoBuild" }
  Copy-Item $f $bin -Force
}

# Profiles: копия создаётся один раз, чтобы не затирать настройки пользователя.
if (-not (Test-Path $profiles)) {
  & robocopy (Join-Path $r1117 'Profiles') $profiles /E /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy Profiles -> ${profiles}: код $LASTEXITCODE" }
  $root = (Resolve-Path $DataRoot).Path.TrimEnd('\') + '\'
  $cfg = @"
<Settings>
  <RootFileSystem Type="RootFileSystem" FullType="EditorLib.IO.RootFileSystem" Version="2">
    <EditorLib.IO.WinFileSystem write="true">
      <fileSystemRoot>$([Security.SecurityElement]::Escape($root))</fileSystemRoot>
    </EditorLib.IO.WinFileSystem>
  </RootFileSystem>
  <EditorLib.Scripts.ScriptsConfig...EditorLib Type="ScriptsConfig" FullType="EditorLib.Scripts.ScriptsConfig">
    <scripts />
  </EditorLib.Scripts.ScriptsConfig...EditorLib>
  <EditorPlugins.Scene.DebugVarsFilterConfig...EditorPlugins Type="DebugVarsFilterConfig" FullType="EditorPlugins.Scene.DebugVarsFilterConfig">
    <CommonDebugVars />
  </EditorPlugins.Scene.DebugVarsFilterConfig...EditorPlugins>
</Settings>
"@
  [IO.File]::WriteAllText((Join-Path $profiles 'PF_Editor.config'), $cfg, (New-Object Text.UTF8Encoding $false))
  Write-Host "Profiles скопированы, корень данных: $root"
}

Write-Host "Готово: $bin\PF_Editor.exe"
if ($Run) { Start-Process (Join-Path $bin 'PF_Editor.exe') -WorkingDirectory $bin }
