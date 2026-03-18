$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root
$LocalBuildConfigPath = Join-Path $Root "scripts/local/build-toolchain.local.json"

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    & $Action
}

function Get-VisualStudioToolchainInfo {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

    if (-not (Test-Path $vsWhere)) {
        return $null
    }

    $json = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -format json
    if ([string]::IsNullOrWhiteSpace($json)) {
        return $null
    }

    $parsed = $null
    try {
        $parsed = $json | ConvertFrom-Json
    }
    catch {
        return $null
    }

    if ($null -eq $parsed -or $parsed.Count -lt 1) {
        return $null
    }

    $entry = $parsed[0]
    if ($null -eq $entry) {
        return $null
    }

    $installationPath = $entry.installationPath
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        return $null
    }

    $msbuild = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) {
        return $null
    }

    $productDisplayVersion = ""
    if ($entry.catalog -and $entry.catalog.productDisplayVersion) {
        $productDisplayVersion = [string]$entry.catalog.productDisplayVersion
    }

    $installationVersion = ""
    if ($entry.installationVersion) {
        $installationVersion = [string]$entry.installationVersion
    }

    $majorVersion = ""
    if (-not [string]::IsNullOrWhiteSpace($installationVersion)) {
        $majorVersion = $installationVersion.Split('.')[0]
    }

    return @{
        compiler = "visualstudio"
        visualStudioVersion = $productDisplayVersion
        visualStudioInstallationVersion = $installationVersion
        visualStudioMajor = $majorVersion
        installationPath = $installationPath
        msbuildPath = $msbuild
    }
}

function Read-LocalBuildConfig {
    if (-not (Test-Path $LocalBuildConfigPath)) {
        return $null
    }

    try {
        $cfg = Get-Content -Path $LocalBuildConfigPath -Raw | ConvertFrom-Json
    }
    catch {
        return $null
    }

    if ($null -eq $cfg) {
        return $null
    }

    if ([string]::IsNullOrWhiteSpace([string]$cfg.msbuildPath)) {
        return $null
    }

    if (-not (Test-Path ([string]$cfg.msbuildPath))) {
        return $null
    }

    return $cfg
}

function Write-LocalBuildConfig {
    param(
        [Parameter(Mandatory = $true)]$ToolchainInfo
    )

    $configDir = Split-Path -Parent $LocalBuildConfigPath
    if (-not (Test-Path $configDir)) {
        New-Item -ItemType Directory -Path $configDir -Force | Out-Null
    }

    $payload = [ordered]@{
        schemaVersion = 1
        compiler = $ToolchainInfo.compiler
        visualStudioVersion = $ToolchainInfo.visualStudioVersion
        visualStudioInstallationVersion = $ToolchainInfo.visualStudioInstallationVersion
        visualStudioMajor = $ToolchainInfo.visualStudioMajor
        installationPath = $ToolchainInfo.installationPath
        msbuildPath = $ToolchainInfo.msbuildPath
        generatedBy = "scripts/Verify-Build.ps1"
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }

    $payload | ConvertTo-Json -Depth 8 | Set-Content -Path $LocalBuildConfigPath -Encoding UTF8
}

function Resolve-MSBuildPath {
    $localConfig = Read-LocalBuildConfig
    if ($null -ne $localConfig) {
        Write-Host "Using local build toolchain config: $LocalBuildConfigPath" -ForegroundColor DarkGray
        return [string]$localConfig.msbuildPath
    }

    $detected = Get-VisualStudioToolchainInfo
    if ($null -eq $detected) {
        return $null
    }

    Write-LocalBuildConfig -ToolchainInfo $detected
    Write-Host "Created local build toolchain config: $LocalBuildConfigPath" -ForegroundColor DarkGray
    return [string]$detected.msbuildPath
}

function Stop-RunningDefectsStudio {
    $running = Get-Process -Name "DefectsStudio" -ErrorAction SilentlyContinue
    if ($null -eq $running) {
        return
    }

    Write-Host "Stopping running DefectsStudio instances to avoid linker file lock..." -ForegroundColor Yellow
    foreach ($process in $running) {
        try {
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
        }
        catch {
            throw "Failed to stop DefectsStudio process (PID $($process.Id)). Close the app and retry."
        }
    }
}

Invoke-Step -Name "Close running DefectsStudio" -Action {
    Stop-RunningDefectsStudio
}

Invoke-Step -Name "Run setup" -Action {
    & (Join-Path $Root "scripts/Setup.bat") --skip-submodule-sync
    if ($LASTEXITCODE -ne 0) {
        throw "Setup failed"
    }
}

$solutionPath = Join-Path $Root "DefectsStudio.sln"
if (-not (Test-Path $solutionPath)) {
    throw "Solution file not found: $solutionPath"
}

$msbuildPath = Resolve-MSBuildPath
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
