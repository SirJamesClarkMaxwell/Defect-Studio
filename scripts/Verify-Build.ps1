$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    & $Action
}

function Get-MSBuildPath {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

    if (-not (Test-Path $vsWhere)) {
        return $null
    }

    $installationPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        return $null
    }

    $msbuild = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $msbuild) {
        return $msbuild
    }

    return $null
}

Invoke-Step -Name "Run setup" -Action {
    & (Join-Path $Root "scripts/Setup.bat")
    if ($LASTEXITCODE -ne 0) {
        throw "Setup failed"
    }
}

$solutionPath = Join-Path $Root "DefectsStudio.sln"
if (-not (Test-Path $solutionPath)) {
    throw "Solution file not found: $solutionPath"
}

$msbuildPath = Get-MSBuildPath
if ($null -eq $msbuildPath) {
    throw "MSBuild was not found. Install Visual Studio 2022 with Desktop development with C++ and MSBuild component."
}

$cpuCount = [Math]::Max([Environment]::ProcessorCount, 1)
$msbuildParallelArgs = @(
    "/m:$cpuCount",
    "/p:UseMultiToolTask=true",
    "/p:CL_MPCount=$cpuCount"
)

Invoke-Step -Name "Build Debug|x64" -Action {
    & $msbuildPath $solutionPath /t:Build /p:Configuration=Debug /p:Platform=x64 @msbuildParallelArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Debug build failed"
    }
}

Invoke-Step -Name "Build Release|x64" -Action {
    & $msbuildPath $solutionPath /t:Build /p:Configuration=Release /p:Platform=x64 @msbuildParallelArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed"
    }
}

Write-Host ""
Write-Host "Build verification passed for Debug and Release." -ForegroundColor Green
