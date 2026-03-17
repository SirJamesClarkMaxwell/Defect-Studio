@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

echo Syncing git submodules...
git submodule sync --recursive
if errorlevel 1 (
    echo Submodule sync failed.
    exit /b 1
)

echo Initializing git submodules...
git submodule update --init --recursive
if errorlevel 1 (
    echo Submodule update failed.
    exit /b 1
)

powershell -ExecutionPolicy Bypass -File scripts\Setup.ps1
if errorlevel 1 (
    echo Setup failed.
    exit /b 1
)

echo Setup complete.
exit /b 0
