@echo off
setlocal

set ROOT_DIR=%~dp0..
cd /d "%ROOT_DIR%"

call scripts\Tooling.bat setup %*
exit /b %errorlevel%
