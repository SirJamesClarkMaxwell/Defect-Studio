@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\Tooling.ps1 %*
exit /b %errorlevel%
