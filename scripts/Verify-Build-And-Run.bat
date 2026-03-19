@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

echo === Verify build ===
call scripts\Verify-Build.bat
set VERIFY_EXIT=%errorlevel%
if not "%VERIFY_EXIT%"=="0" (
    echo Verify step failed with exit code %VERIFY_EXIT%.
    echo Build verification failed. Application will not start.
    exit /b 1
)

echo === Run application ===
call scripts\Run.bat
set RUN_EXIT=%errorlevel%
if not "%RUN_EXIT%"=="0" (
    echo Run step failed with exit code %RUN_EXIT%.
    echo Application failed to start.
    exit /b 1
)

exit /b 0
