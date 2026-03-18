param(
    [switch]$SkipSubmoduleSync
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$PremakeDir = Join-Path $Root "tools/premake"
$PremakeExe = Join-Path $PremakeDir "premake5.exe"
$LocalBuildConfigPath = Join-Path $Root "scripts/local/build-toolchain.local.json"

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

    $installationPath = [string]$entry.installationPath
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

function Get-AvailablePlatformToolsets {
    param(
        [Parameter(Mandatory = $true)][string]$InstallationPath
    )

    $toolsetsPath = Join-Path $InstallationPath "MSBuild\Microsoft\VC\v180\Platforms\x64\PlatformToolsets"
    if (-not (Test-Path $toolsetsPath)) {
        return @()
    }

    $toolsets = @(
        Get-ChildItem -Path $toolsetsPath -Directory -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Name |
            Where-Object { $_ -match '^v\d+$' }
    )

    if ($toolsets.Count -lt 1) {
        return @()
    }

    return $toolsets | Sort-Object { [int]($_ -replace '[^0-9]', '') } -Descending
}

function Resolve-PlatformToolset {
    param(
        [Parameter(Mandatory = $true)][string]$InstallationPath
    )

    if (-not [string]::IsNullOrWhiteSpace($env:DEFECTSSTUDIO_PLATFORM_TOOLSET)) {
        return [string]$env:DEFECTSSTUDIO_PLATFORM_TOOLSET
    }

    $availableToolsets = Get-AvailablePlatformToolsets -InstallationPath $InstallationPath
    if ($availableToolsets.Count -gt 0) {
        return [string]$availableToolsets[0]
    }

    return $null
}

function Write-LocalBuildConfig {
    param(
        [Parameter(Mandatory = $true)]$ToolchainInfo,
        [Parameter(Mandatory = $true)][string]$PremakeAction,
        [string]$PlatformToolset
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
        premakeAction = $PremakeAction
        generatedBy = "scripts/Setup.ps1"
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    }

    if (-not [string]::IsNullOrWhiteSpace($PlatformToolset)) {
        $payload.platformToolset = $PlatformToolset
    }

    $payload | ConvertTo-Json -Depth 8 | Set-Content -Path $LocalBuildConfigPath -Encoding UTF8
}

function Refresh-LocalBuildConfig {
    $toolchain = Get-VisualStudioToolchainInfo
    if ($null -eq $toolchain) {
        return $null
    }

    $majorVersion = 0
    if (-not [string]::IsNullOrWhiteSpace([string]$toolchain.visualStudioMajor)) {
        $majorVersion = [int]$toolchain.visualStudioMajor
    }

    $premakeAction = Resolve-PremakeActionFromVsMajor -Major $majorVersion
    if ([string]::IsNullOrWhiteSpace($premakeAction)) {
        $premakeAction = "vs2022"
    }

    $platformToolset = Resolve-PlatformToolset -InstallationPath ([string]$toolchain.installationPath)

    Write-LocalBuildConfig -ToolchainInfo $toolchain -PremakeAction $premakeAction -PlatformToolset $platformToolset
    Write-Host "Updated local build toolchain config: $LocalBuildConfigPath" -ForegroundColor DarkGray
}

function Resolve-PremakeActionFromVsMajor {
    param(
        [Parameter(Mandatory = $true)][int]$Major
    )

    if ($Major -ge 17) {
        # Premake action vs2022 targets VS 2022 toolset and is the best available option for VS17+.
        return "vs2022"
    }

    if ($Major -eq 16) {
        return "vs2019"
    }

    return $null
}

function Get-PreferredPremakeAction {
    $defaultAction = "vs2022"

    if (-not [string]::IsNullOrWhiteSpace($env:DEFECTSSTUDIO_PREMAKE_ACTION)) {
        return $env:DEFECTSSTUDIO_PREMAKE_ACTION
    }

    if (Test-Path $LocalBuildConfigPath) {
        try {
            $cfg = Get-Content -Path $LocalBuildConfigPath -Raw | ConvertFrom-Json
            if ($cfg -and -not [string]::IsNullOrWhiteSpace([string]$cfg.premakeAction)) {
                return [string]$cfg.premakeAction
            }

            if ($cfg -and $cfg.visualStudioMajor) {
                $major = [int]$cfg.visualStudioMajor
                $resolvedAction = Resolve-PremakeActionFromVsMajor -Major $major
                if (-not [string]::IsNullOrWhiteSpace($resolvedAction)) {
                    return $resolvedAction
                }
            }
        }
        catch {
            # Ignore malformed local config and fallback to detection/default.
        }
    }

    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        try {
            $installationVersion = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationVersion
            if (-not [string]::IsNullOrWhiteSpace($installationVersion)) {
                $majorToken = $installationVersion.Split('.')[0]
                $major = [int]$majorToken
                $resolvedAction = Resolve-PremakeActionFromVsMajor -Major $major
                if (-not [string]::IsNullOrWhiteSpace($resolvedAction)) {
                    if ($major -gt 17) {
                        Write-Host "Detected Visual Studio major version $major; using Premake action '$resolvedAction'." -ForegroundColor DarkYellow
                    }

                    return $resolvedAction
                }
            }
        }
        catch {
            # Ignore detection failure and fallback to default.
        }
    }

    return $defaultAction
}

if (-not (Test-Path $PremakeExe)) {
    New-Item -ItemType Directory -Force -Path $PremakeDir | Out-Null

    $PremakeZip = Join-Path $env:TEMP "premake-5.0.0-beta2-windows.zip"
    $PremakeUrl = "https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip"

    Write-Host "Downloading Premake5..."
    Invoke-WebRequest -Uri $PremakeUrl -OutFile $PremakeZip
    Expand-Archive -Path $PremakeZip -DestinationPath $PremakeDir -Force
}

if ($SkipSubmoduleSync) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "FetchDeps.ps1") -SkipSubmoduleSync
}
else {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "FetchDeps.ps1")
}

Refresh-LocalBuildConfig

$premakeAction = Get-PreferredPremakeAction
Write-Host "Generating Visual Studio solution using Premake action '$premakeAction'..."
& $PremakeExe $premakeAction

if ($LASTEXITCODE -ne 0) {
    throw "Premake generation failed"
}
