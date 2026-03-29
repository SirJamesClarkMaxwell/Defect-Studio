param(
    [switch]$SkipSubmoduleSync
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

& (Join-Path $PSScriptRoot "Tooling.ps1") "setup" @(
    if ($SkipSubmoduleSync) { "--skip-submodule-sync" }
)
exit $LASTEXITCODE
