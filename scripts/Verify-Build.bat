@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\Verify-Build.ps1
if errorlevel 1 (
    echo Build verification failed.
    exit /b 1
)

echo Build verification succeeded.
exit /b 0
