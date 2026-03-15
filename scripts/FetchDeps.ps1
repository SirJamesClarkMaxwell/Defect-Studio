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
Ensure-GitClone -Repo "https://github.com/ocornut/imgui.git" -Destination "vendor/imgui" -Branch "docking"
Ensure-GitClone -Repo "https://github.com/g-truc/glm.git" -Destination "vendor/glm"
