$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$PremakeDir = Join-Path $Root "tools/premake"
$PremakeExe = Join-Path $PremakeDir "premake5.exe"

if (-not (Test-Path $PremakeExe)) {
    New-Item -ItemType Directory -Force -Path $PremakeDir | Out-Null

    $PremakeZip = Join-Path $env:TEMP "premake-5.0.0-beta2-windows.zip"
    $PremakeUrl = "https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip"

    Write-Host "Downloading Premake5..."
    Invoke-WebRequest -Uri $PremakeUrl -OutFile $PremakeZip
    Expand-Archive -Path $PremakeZip -DestinationPath $PremakeDir -Force
}

& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "FetchDeps.ps1")

Write-Host "Generating Visual Studio 2022 solution..."
& $PremakeExe vs2022

if ($LASTEXITCODE -ne 0) {
    throw "Premake generation failed"
}
