@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

echo === Verify build ===
call scripts\Verify-Build.bat
if errorlevel 1 (
    echo Build verification failed. Application will not start.
    exit /b 1
)

set DEBUG_EXE=%ROOT_DIR%\bin\Debug-windows-x86_64\DefectsStudio\DefectsStudio.exe
set RELEASE_EXE=%ROOT_DIR%\bin\Release-windows-x86_64\DefectsStudio\DefectsStudio.exe

if exist "%DEBUG_EXE%" (
    echo Starting Debug build...
    start "DefectsStudio" "%DEBUG_EXE%"
    exit /b 0
)

if exist "%RELEASE_EXE%" (
    echo Starting Release build...
    start "DefectsStudio" "%RELEASE_EXE%"
    exit /b 0
)

echo Could not find DefectsStudio executable in Debug or Release output folders.
exit /b 1
