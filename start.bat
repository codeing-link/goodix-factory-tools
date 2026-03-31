@echo off
setlocal

set PORT=%1
set BAUD=%2

REM 默认值
if "%PORT%"=="" set PORT=COM28
if "%BAUD%"=="" set BAUD=115200

cd /d %~dp0

echo ==========================================
echo   GH Backend Runner
echo ==========================================
echo Port : %PORT%
echo Baud : %BAUD%
echo.

if not exist "gh_backend.exe" (
    echo [ERROR] gh_backend.exe not found!
    pause
    exit /b 1
)

gh_backend.exe --port %PORT% --baud %BAUD%

echo.
echo Program exited.
pause