@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

set SKIP_SUBMODULE_SYNC=0
if /I "%~1"=="--skip-submodule-sync" set SKIP_SUBMODULE_SYNC=1

if "%SKIP_SUBMODULE_SYNC%"=="1" (
    echo Skipping submodule sync/update.
) else (
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
)

if "%SKIP_SUBMODULE_SYNC%"=="1" (
    powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\Setup.ps1 -SkipSubmoduleSync
) else (
    powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\Setup.ps1
)
if errorlevel 1 (
    echo Setup failed.
    exit /b 1
)

echo Setup complete.
exit /b 0
