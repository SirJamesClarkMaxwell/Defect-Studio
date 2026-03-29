$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

& (Join-Path $PSScriptRoot "Tooling.ps1") "verify-build" @args
exit $LASTEXITCODE
