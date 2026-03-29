$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$debugExe = Join-Path $Root "bin/Debug-windows-x86_64/DefectsStudio/DefectsStudio.exe"
$releaseExe = Join-Path $Root "bin/Release-windows-x86_64/DefectsStudio/DefectsStudio.exe"

if (Test-Path $debugExe) {
    Write-Host "Starting Debug build..." -ForegroundColor Cyan
    $process = Start-Process -FilePath $debugExe -PassThru
    Start-Sleep -Milliseconds 350
    if ($process.HasExited) {
        Write-Host "DefectsStudio exited immediately after launch (Debug)." -ForegroundColor Red
        exit 1
    }
    exit 0
}

if (Test-Path $releaseExe) {
    Write-Host "Starting Release build..." -ForegroundColor Cyan
    $process = Start-Process -FilePath $releaseExe -PassThru
    Start-Sleep -Milliseconds 350
    if ($process.HasExited) {
        Write-Host "DefectsStudio exited immediately after launch (Release)." -ForegroundColor Red
        exit 1
    }
    exit 0
}

Write-Host "Could not find DefectsStudio executable in Debug or Release output folders." -ForegroundColor Red
Write-Host "Build first, e.g. with scripts\\Tooling.bat verify-build" -ForegroundColor Yellow
exit 1
