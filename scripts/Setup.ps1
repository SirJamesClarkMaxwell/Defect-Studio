param(
    [switch]$SkipSubmoduleSync
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$PremakeDir = Join-Path $Root "tools/premake"
$PremakeExe = Join-Path $PremakeDir "premake5.exe"
$LocalBuildConfigPath = Join-Path $Root "scripts/local/build-toolchain.local.json"

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

$premakeAction = Get-PreferredPremakeAction
Write-Host "Generating Visual Studio solution using Premake action '$premakeAction'..."
& $PremakeExe $premakeAction

if ($LASTEXITCODE -ne 0) {
    throw "Premake generation failed"
}
