@echo off
REM ============================================================
REM build_and_run.bat - Auto build, package and run GH Protocol backend (Windows)
REM Usage:
REM   Double click to run
REM   Command line: build_and_run.bat --port COM3 --baud 115200
REM ============================================================

setlocal enabledelayedexpansion

REM Script and directories
set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build
set PACKAGE_DIR=%SCRIPT_DIR%package

echo ============================================================
echo   GH Protocol Build Script (Windows)
echo ============================================================
echo   Project Dir: %SCRIPT_DIR%
echo   Build Dir  : %BUILD_DIR%
echo   Package Dir: %PACKAGE_DIR%
echo.

REM ------------------------------------------------------------
REM 1. Create build directory
REM ------------------------------------------------------------
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM ------------------------------------------------------------
REM 2. Run CMake configure
REM ------------------------------------------------------------
echo [1/5] Running CMake configure...
cmake .. -G "MinGW Makefiles"
if %errorlevel% neq 0 (
    echo [ERROR] CMake configure failed!
    echo Please ensure:
    echo   - CMake is installed
    echo   - MinGW is installed
    echo   - cmake is in PATH
    pause
    exit /b %errorlevel%
)
echo.

REM ------------------------------------------------------------
REM 3. Build project
REM ------------------------------------------------------------
echo [2/5] Building project...
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

echo [3/5] Build success: %EXE_PATH%
echo.

REM ------------------------------------------------------------
REM 5. Prepare package directory
REM ------------------------------------------------------------
echo [4/5] Packaging executable and runtime DLLs...

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"

copy /Y "%EXE_PATH%" "%PACKAGE_DIR%\"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to copy gh_backend.exe
    pause
    exit /b 1
)

REM Find gcc location
set GCC_PATH=
for /f "delims=" %%i in ('where gcc 2^>nul') do (
    set GCC_PATH=%%i
    goto :gcc_found
)

:gcc_found
if "%GCC_PATH%"=="" (
    echo [WARNING] gcc not found in PATH, cannot auto-copy runtime DLLs.
    echo You may need to manually copy MinGW runtime DLLs.
    goto :package_done
)

for %%i in ("%GCC_PATH%") do set GCC_DIR=%%~dpi

echo   GCC Path: %GCC_PATH%
echo   GCC Dir : %GCC_DIR%

REM Copy common MinGW runtime DLLs
call :copy_dll "%GCC_DIR%libwinpthread-1.dll"
call :copy_dll "%GCC_DIR%libgcc_s_seh-1.dll"
call :copy_dll "%GCC_DIR%libstdc++-6.dll"

REM Some MinGW versions may use different libgcc name
if not exist "%PACKAGE_DIR%\libgcc_s_seh-1.dll" (
    call :copy_dll "%GCC_DIR%libgcc_s_dw2-1.dll"
)

:package_done
echo.
echo Package completed: %PACKAGE_DIR%
echo.

REM ------------------------------------------------------------
REM 6. Run executable from package directory
REM ------------------------------------------------------------
echo [5/5] Starting backend service...
echo ============================================================
echo.

cd /d "%PACKAGE_DIR%"
"%PACKAGE_DIR%\gh_backend.exe" %*

if %errorlevel% neq 0 (
    echo.
    echo Backend exited with error.
    pause
)

exit /b 0

REM ------------------------------------------------------------
REM Function: copy dll if exists
REM ------------------------------------------------------------
:copy_dll
if exist %1 (
    echo   Copying %~nx1
    copy /Y %1 "%PACKAGE_DIR%\" >nul
) else (
    echo   [INFO] Not found: %~nx1
)
exit /b 0