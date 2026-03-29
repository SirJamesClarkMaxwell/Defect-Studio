param(
    [string]$TracyBuildDir = "D:\t\tracy-build",
    [switch]$SkipTracyBuild
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$tracySourceDir = Join-Path $Root "vendor/tracy/profiler"
if (-not (Test-Path $tracySourceDir)) {
    Write-Host "Tracy submodule not found at vendor/tracy." -ForegroundColor Red
    Write-Host "Run: git submodule update --init --recursive" -ForegroundColor Yellow
    exit 1
}

$tracyExe = Join-Path $TracyBuildDir "Release/tracy-profiler.exe"

if (-not (Test-Path $tracyExe)) {
    if ($SkipTracyBuild) {
        Write-Host "Tracy executable not found and -SkipTracyBuild was provided." -ForegroundColor Red
        Write-Host "Expected: $tracyExe" -ForegroundColor Yellow
        exit 1
    }

    Write-Host "Building Tracy profiler (first run only)..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $TracyBuildDir | Out-Null

    & cmake -S $tracySourceDir -B $TracyBuildDir -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configure failed while building Tracy profiler." -ForegroundColor Red
        exit $LASTEXITCODE
    }

    & cmake --build $TracyBuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake build failed while building Tracy profiler." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

$debugExe = Join-Path $Root "bin/Debug-windows-x86_64/DefectsStudio/DefectsStudio.exe"
$releaseExe = Join-Path $Root "bin/Release-windows-x86_64/DefectsStudio/DefectsStudio.exe"
$appAlreadyRunning = Get-Process -Name "DefectsStudio" -ErrorAction SilentlyContinue

if (-not $appAlreadyRunning) {
    if (Test-Path $debugExe) {
        Write-Host "Starting DefectsStudio (Debug)..." -ForegroundColor Cyan
        Start-Process -FilePath $debugExe | Out-Null
    }
    elseif (Test-Path $releaseExe) {
        Write-Host "Starting DefectsStudio (Release)..." -ForegroundColor Cyan
        Start-Process -FilePath $releaseExe | Out-Null
    }
    else {
        Write-Host "Could not find DefectsStudio executable in Debug or Release output folders." -ForegroundColor Red
        Write-Host "Build first, e.g. with scripts\\Tooling.bat verify-build" -ForegroundColor Yellow
        exit 1
    }
}
else {
    Write-Host "DefectsStudio is already running." -ForegroundColor DarkYellow
}

Start-Sleep -Milliseconds 700

Write-Host "Starting Tracy Profiler GUI..." -ForegroundColor Cyan
Start-Process -FilePath $tracyExe | Out-Null

Write-Host "Done. In Tracy: connect to localhost and select DefectsStudio." -ForegroundColor Green
exit 0
