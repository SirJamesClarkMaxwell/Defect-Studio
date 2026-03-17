$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

Write-Host "Syncing git submodules..."
& git submodule sync --recursive
if ($LASTEXITCODE -ne 0) {
    throw "git submodule sync failed"
}

Write-Host "Initializing/updating git submodules..."
& git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    throw "git submodule update failed"
}

foreach ($requiredPath in @("vendor/glfw", "vendor/glad", "vendor/imgui", "vendor/glm")) {
    if (-not (Test-Path $requiredPath)) {
        throw "Missing required dependency path: $requiredPath"
    }
}

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    throw "uv is required to generate GLAD in an isolated virtual environment. Install uv first."
}

$VenvPython = Join-Path $Root ".venv/Scripts/python.exe"
if (-not (Test-Path $VenvPython)) {
    Write-Host "Creating project venv via uv..."
    & uv venv .venv
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create .venv via uv"
    }
}

Write-Host "Installing GLAD generator dependencies into .venv..."
& uv pip install --python $VenvPython Jinja2
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install Jinja2 in .venv"
}

$GladOutputHeader = "vendor/glad_gen/include/glad/gl.h"
$GladOutputSource = "vendor/glad_gen/src/gl.c"
if (-not (Test-Path $GladOutputHeader) -or -not (Test-Path $GladOutputSource)) {
    Write-Host "Generating GLAD loader files (OpenGL 4.1 core)..."
    Push-Location "vendor/glad"
    try {
        & $VenvPython -m glad --api gl:core=4.1 --out-path "..\glad_gen" c
        if ($LASTEXITCODE -ne 0) {
            throw "GLAD generation failed"
        }
    }
    finally {
        Pop-Location
    }
}
