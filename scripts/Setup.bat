@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

powershell -ExecutionPolicy Bypass -File scripts\Setup.ps1
if errorlevel 1 (
    echo Setup failed.
    exit /b 1
)

echo Setup complete.
exit /b 0
