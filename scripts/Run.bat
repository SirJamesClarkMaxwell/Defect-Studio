@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

powershell -ExecutionPolicy Bypass -File scripts\Run.ps1
exit /b %errorlevel%
