@echo off
REM ============================================================
REM build_and_run.bat - Auto build and run GH Protocol backend (Windows)
REM Usage:
REM   Double click to run (default simulator mode)
REM   Command line: build_and_run.bat --port COM3 --baud 115200
REM ============================================================

setlocal

REM Script and build directory
set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build

echo ============================================================
echo   GH Protocol Build Script (Windows)
echo ============================================================
echo   Project Dir: %SCRIPT_DIR%
echo   Build Dir: %BUILD_DIR%
echo.

REM ------------------------------------------------------------
REM 1. Create build directory
REM ------------------------------------------------------------
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM ------------------------------------------------------------
REM 2. Run CMake configure
REM ------------------------------------------------------------
echo [1/3] Running CMake configure...
cmake .. -G "MinGW Makefiles"
if %errorlevel% neq 0 (
    echo [ERROR] CMake configure failed!
    echo Please ensure:
    echo   - CMake is installed
    echo   - Visual Studio C++ or MinGW is installed
    echo   - cmake is in PATH
    pause
    exit /b %errorlevel%
)
echo.

REM ------------------------------------------------------------
REM 3. Build project
REM ------------------------------------------------------------
echo [2/3] Building project...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b %errorlevel%
)
echo.

REM ------------------------------------------------------------
REM 4. Locate gh_backend.exe
REM ------------------------------------------------------------
set EXE_PATH=

if exist "%BUILD_DIR%\gh_backend.exe" (
    set EXE_PATH=%BUILD_DIR%\gh_backend.exe
)

if exist "%BUILD_DIR%\Release\gh_backend.exe" (
    set EXE_PATH=%BUILD_DIR%\Release\gh_backend.exe
)

if exist "%BUILD_DIR%\Debug\gh_backend.exe" (
    set EXE_PATH=%BUILD_DIR%\Debug\gh_backend.exe
)

if "%EXE_PATH%"=="" (
    echo [ERROR] Cannot find gh_backend.exe!
    pause
    exit /b 1
)

echo [3/3] Build success: %EXE_PATH%
echo ============================================================
echo Starting backend service...
echo.

REM ------------------------------------------------------------
REM 5. Run executable from project root
REM ------------------------------------------------------------
cd /d "%SCRIPT_DIR%"
"%EXE_PATH%" %*

if %errorlevel% neq 0 (
    echo.
    echo Backend exited with error.
    pause
)
