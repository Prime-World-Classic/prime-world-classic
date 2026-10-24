# Prime World: прогрев игровых данных в файловом кэше Windows.
# Читает все файлы из Data, Localization и Bin, чтобы первый запуск игры
# после включения компьютера грузился за секунды, а не за минуты.
#
# Запуск вручную:            warmup_data.bat
# Добавить в автозагрузку:   warmup_data.bat -Install
# Убрать из автозагрузки:    warmup_data.bat -Uninstall

param(
    [switch]$Install,
    [switch]$Uninstall,
    [int]$DelaySeconds = 0
)

$ErrorActionPreference = 'Continue'
$root     = $PSScriptRoot
$branch   = Join-Path $root 'pw\branches\r1117'
$folders  = @('Data', 'Localization', 'Bin') | ForEach-Object { Join-Path $branch $_ }
$startup  = [Environment]::GetFolderPath('Startup')
$lnkPath  = Join-Path $startup 'Prime World - warmup data.lnk'
$batPath  = Join-Path $root 'warmup_data.bat'

if ($Install) {
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut($lnkPath)
    $lnk.TargetPath       = $batPath
    $lnk.Arguments        = '-DelaySeconds 60'   # даём Windows спокойно загрузиться
    $lnk.WorkingDirectory = $root
    $lnk.WindowStyle      = 7                     # свёрнутое окно
    $lnk.Description      = 'Prime World: прогрев игровых данных'
    $lnk.Save()
    Write-Host "Добавлено в автозагрузку: $lnkPath"
    exit 0
}

if ($Uninstall) {
    if (Test-Path $lnkPath) { Remove-Item $lnkPath -Force; Write-Host 'Убрано из автозагрузки.' }
    else { Write-Host 'В автозагрузке ярлыка нет.' }
    exit 0
}

# Не мешаем другим программам: пониженный приоритет процессора.
try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch {}

if ($DelaySeconds -gt 0) {
    Write-Host "Ждём $DelaySeconds с перед прогревом..."
    Start-Sleep -Seconds $DelaySeconds
}

$sw     = [Diagnostics.Stopwatch]::StartNew()
$buffer = New-Object byte[] (1MB)
$files  = 0
$bytes  = [long]0
$errors = 0

foreach ($folder in $folders) {
    if (-not (Test-Path $folder)) { Write-Host "Нет папки, пропускаем: $folder"; continue }
    Write-Host "Читаем $folder ..."
    foreach ($file in [IO.Directory]::EnumerateFiles($folder, '*', [IO.SearchOption]::AllDirectories)) {
        try {
            $fs = New-Object IO.FileStream($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite, 65536, [IO.FileOptions]::SequentialScan)
            try {
                while (($n = $fs.Read($buffer, 0, $buffer.Length)) -gt 0) { $bytes += $n }
            } finally { $fs.Dispose() }
            $files++
            if ($files % 5000 -eq 0) {
                Write-Host ("  {0} файлов, {1:N0} МБ, {2:N0} с" -f $files, ($bytes / 1MB), $sw.Elapsed.TotalSeconds)
            }
        } catch { $errors++ }
    }
}

$sw.Stop()
Write-Host ("Готово: {0} файлов, {1:N0} МБ за {2:N0} с. Ошибок чтения: {3}." -f $files, ($bytes / 1MB), $sw.Elapsed.TotalSeconds, $errors)
