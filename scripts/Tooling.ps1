param(
    [Parameter(Position = 0)]
    [ValidateSet("setup", "verify-build", "python")]
    [string]$Task = "setup",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$TaskArgs
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$RequiredPythonVersion = "3.13"
$VenvPython = Join-Path $Root ".venv\Scripts\python.exe"

function Fail([string]$Message) {
    Write-Host $Message -ForegroundColor Red
    exit 1
}

function Write-Section([string]$Title) {
    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Get-UvPath {
    $command = Get-Command uv -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $commonPaths = @(
        (Join-Path $env:USERPROFILE ".local\bin\uv.exe"),
        (Join-Path $env:USERPROFILE ".cargo\bin\uv.exe")
    )
    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            return $path
        }
    }

    return $null
}

function Install-Uv {
    Write-Section "Install uv"
    $installerPath = Join-Path $env:TEMP "uv-install.ps1"
    Invoke-WebRequest -UseBasicParsing -Uri "https://astral.sh/uv/install.ps1" -OutFile $installerPath
    & powershell -NoProfile -ExecutionPolicy Bypass -File $installerPath
    if ($LASTEXITCODE -ne 0) {
        Fail "Failed to install uv."
    }

    $uvPath = Get-UvPath
    if ([string]::IsNullOrWhiteSpace($uvPath)) {
        Fail "uv installation finished, but `uv` is still not visible in this shell. Reopen the terminal and rerun."
    }

    return $uvPath
}

function Ensure-Uv {
    $uvPath = Get-UvPath
    if (-not [string]::IsNullOrWhiteSpace($uvPath)) {
        return $uvPath
    }
    return Install-Uv
}

function Test-PythonVersion([string]$ExecutablePath) {
    if ([string]::IsNullOrWhiteSpace($ExecutablePath) -or -not (Test-Path $ExecutablePath)) {
        return $false
    }

    $versionOutput = & $ExecutablePath -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($versionOutput)) {
        return $false
    }

    try {
        return ([version]($versionOutput.Trim() + ".0")) -ge [version]($RequiredPythonVersion + ".0")
    }
    catch {
        return $false
    }
}

function Try-ResolvePythonExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandName,

        [string[]]$PrefixArgs = @()
    )

    if (-not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
        return $null
    }

    try {
        $resolved = & $CommandName @($PrefixArgs + @("-c", "import sys; print(sys.executable)")) 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($resolved)) {
            return $resolved.Trim()
        }
    }
    catch {
        return $null
    }

    return $null
}

function Find-SystemPython {
    $candidates = @()

    if ($env:VIRTUAL_ENV) {
        $activeVenvPython = Join-Path $env:VIRTUAL_ENV "Scripts\python.exe"
        if (Test-Path $activeVenvPython) {
            $candidates += $activeVenvPython
        }
    }

    $python = Try-ResolvePythonExecutable -CommandName "python"
    if (-not [string]::IsNullOrWhiteSpace($python)) {
        $candidates += $python
    }

    $py311 = Try-ResolvePythonExecutable -CommandName "py" -PrefixArgs @("-3.11")
    if (-not [string]::IsNullOrWhiteSpace($py311)) {
        $candidates += $py311
    }

    $py3 = Try-ResolvePythonExecutable -CommandName "py" -PrefixArgs @("-3")
    if (-not [string]::IsNullOrWhiteSpace($py3)) {
        $candidates += $py3
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-PythonVersion $candidate) {
            return $candidate
        }
    }

    return $null
}

function Ensure-Venv([string]$UvPath) {
    if ((Test-Path $VenvPython) -and -not (Test-PythonVersion $VenvPython)) {
        Write-Section "Recreate invalid .venv"
        Remove-Item (Join-Path $Root ".venv") -Recurse -Force
    }

    if (-not (Test-Path $VenvPython)) {
        $systemPython = Find-SystemPython
        if (-not [string]::IsNullOrWhiteSpace($systemPython)) {
            Write-Section "Create virtual environment"
            & $UvPath venv .venv --python $systemPython
            if ($LASTEXITCODE -ne 0) {
                Fail "Failed to create .venv from the existing Python installation."
            }
        }
        else {
            Write-Section "Install managed Python"
            & $UvPath python install $RequiredPythonVersion
            if ($LASTEXITCODE -ne 0) {
                Fail "uv failed to install Python $RequiredPythonVersion."
            }

            Write-Section "Create virtual environment"
            & $UvPath venv .venv --python $RequiredPythonVersion
            if ($LASTEXITCODE -ne 0) {
                Fail "Failed to create .venv from the uv-managed Python installation."
            }
        }
    }

    Write-Section "Sync Python tooling environment"
    & $UvPath sync
    if ($LASTEXITCODE -ne 0) {
        Fail "Failed to sync the Python tooling environment with uv."
    }

    if (-not (Test-Path $VenvPython)) {
        Fail "Virtual environment sync finished, but .venv\\Scripts\\python.exe is missing."
    }

    return $VenvPython
}

function Sync-Submodules([bool]$Skip) {
    if ($Skip) {
        Write-Host "Skipping git submodule sync/update."
        return
    }

    Write-Section "Sync git submodules"
    & git submodule sync --recursive
    if ($LASTEXITCODE -ne 0) {
        Fail "Submodule sync failed."
    }

    & git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        Fail "Submodule update failed."
    }
}

$skipSubmoduleSync = $TaskArgs -contains "--skip-submodule-sync"
$forwardArgs = @($TaskArgs | Where-Object { $_ -ne "--skip-submodule-sync" })

$uvPath = Ensure-Uv
$venvPythonPath = Ensure-Venv $uvPath

if ($Task -in @("setup", "verify-build")) {
    Sync-Submodules $skipSubmoduleSync
    $forwardArgs = @("--skip-submodule-sync") + $forwardArgs
}

switch ($Task) {
    "setup" {
        & $venvPythonPath (Join-Path $PSScriptRoot "setup.py") @forwardArgs
        exit $LASTEXITCODE
    }
    "verify-build" {
        & $venvPythonPath (Join-Path $PSScriptRoot "verify_build.py") @forwardArgs
        exit $LASTEXITCODE
    }
    "python" {
        & $venvPythonPath @forwardArgs
        exit $LASTEXITCODE
    }
}
