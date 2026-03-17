$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

function Ensure-GitClone {
    param(
        [Parameter(Mandatory = $true)][string]$Repo,
        [Parameter(Mandatory = $true)][string]$Destination,
        [string]$Branch = "",
        [int]$Depth = 1
    )

    if (Test-Path $Destination) {
        Write-Host "Dependency already present: $Destination"
        return
    }

    $cloneArgs = @("clone", "--depth", "$Depth")
    if ($Branch -ne "") {
        $cloneArgs += @("--branch", $Branch)
    }
    $cloneArgs += @($Repo, $Destination)

    Write-Host "Cloning $Repo -> $Destination"
    & git @cloneArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone $Repo"
    }
}

New-Item -ItemType Directory -Force -Path "vendor" | Out-Null

Ensure-GitClone -Repo "https://github.com/glfw/glfw.git" -Destination "vendor/glfw"
Ensure-GitClone -Repo "https://github.com/Dav1dde/glad.git" -Destination "vendor/glad"
Ensure-GitClone -Repo "https://github.com/ocornut/imgui.git" -Destination "vendor/imgui" -Branch "docking"
Ensure-GitClone -Repo "https://github.com/g-truc/glm.git" -Destination "vendor/glm"

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
