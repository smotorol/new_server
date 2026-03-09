@echo off
setlocal ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

echo ========================================
echo Setting up vcpkg for Windows
echo ========================================

set "MY_VCPKG_ROOT=C:\dev\vcpkg"
set "VCPKG_TRIPLET=x64-windows-static"

if not exist "%MY_VCPKG_ROOT%" (
    echo Installing vcpkg into %MY_VCPKG_ROOT%
    git clone https://github.com/microsoft/vcpkg "%MY_VCPKG_ROOT%"
	if errorlevel 1 exit /b 1
)

if not exist "%MY_VCPKG_ROOT%\vcpkg.exe" (
    echo Bootstrapping vcpkg
    call "%MY_VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
	if errorlevel 1 exit /b 1
)

setx MY_VCPKG_ROOT "%MY_VCPKG_ROOT%" >nul

echo MY_VCPKG_ROOT registered as:
echo %MY_VCPKG_ROOT%

echo Please restart terminal or Visual Studio.


set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "BUILD_TYPE=Debug"
set "DO_CONFIGURE=1"
set "DO_BUILD=1"
set "FORCE_CLEAN=0"
set "JOBS="
set "PRESET="
set "RUN_VS_OPEN=0"
set "RUN_WSL_SETUP=0"
set "WSL_DISTRO="

:parse
if "%~1"=="" goto args_done
if /I "%~1"=="--build-type" (
  set "BUILD_TYPE=%~2"
  shift & shift & goto parse
)
if /I "%~1"=="--preset" (
  set "PRESET=%~2"
  shift & shift & goto parse
)
if /I "%~1"=="--no-configure" (
  set "DO_CONFIGURE=0"
  shift & goto parse
)
if /I "%~1"=="--no-build" (
  set "DO_BUILD=0"
  shift & goto parse
)
if /I "%~1"=="--force-clean" (
  set "FORCE_CLEAN=1"
  shift & goto parse
)
if /I "%~1"=="--jobs" (
  set "JOBS=%~2"
  shift & shift & goto parse
)
if /I "%~1"=="--open-vs" (
  set "RUN_VS_OPEN=1"
  shift & goto parse
)
if /I "%~1"=="--run-wsl-setup" (
  set "RUN_WSL_SETUP=1"
  shift & goto parse
)
if /I "%~1"=="--wsl-distro" (
  set "WSL_DISTRO=%~2"
  shift & shift & goto parse
)
if /I "%~1"=="-h" goto usage
if /I "%~1"=="--help" goto usage

echo [ERROR] Unknown option: %~1
exit /b 1

:args_done
if /I "%BUILD_TYPE%"=="Debug" (
  if "%PRESET%"=="" set "PRESET=win-debug"
)
if /I "%BUILD_TYPE%"=="Release" (
  if "%PRESET%"=="" set "PRESET=win-release"
)

if not exist "%ROOT_DIR%\CMakeLists.txt" (
  echo [ERROR] CMakeLists.txt not found in %ROOT_DIR%
  exit /b 1
)
if not exist "%ROOT_DIR%\CMakePresets.json" (
  echo [ERROR] CMakePresets.json not found in %ROOT_DIR%
  exit /b 1
)
if not exist "%ROOT_DIR%\vcpkg.json" (
  echo [ERROR] vcpkg.json not found in %ROOT_DIR%
  exit /b 1
)

where git >nul 2>nul || (echo [ERROR] git not found & exit /b 1)
where cmake >nul 2>nul || (echo [ERROR] cmake not found & exit /b 1)

call :prepare_repo "%ROOT_DIR%\external\inipp_repo" "https://github.com/mcmtroffaes/inipp" || exit /b 1

echo [INFO] Bootstrapping vcpkg
call "%MY_VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1

echo [INFO] Installing vcpkg dependencies for %VCPKG_TRIPLET%
call "%MY_VCPKG_ROOT%\vcpkg.exe" install --triplet %VCPKG_TRIPLET% --x-manifest-root="%ROOT_DIR%" || exit /b 1

if "%FORCE_CLEAN%"=="1" (
  if exist "%ROOT_DIR%\build_vs" (
    echo [INFO] Removing build_vs cache
    rmdir /s /q "%ROOT_DIR%\build_vs"
  )
)

if "%DO_CONFIGURE%"=="1" (
  echo [INFO] Configuring with preset: %PRESET%
  cmake --preset "%PRESET%" || exit /b 1
)

if "%DO_BUILD%"=="1" (
  echo [INFO] Building with preset: %PRESET%
  if not "%JOBS%"=="" (
    cmake --build --preset "%PRESET%" --parallel %JOBS% || exit /b 1
  ) else (
    cmake --build --preset "%PRESET%" || exit /b 1
  )
)

if "%RUN_WSL_SETUP%"=="1" (
  call :run_wsl_setup || exit /b 1
)

if "%RUN_VS_OPEN%"=="1" (
  start "" devenv.exe "%ROOT_DIR%"
)

echo.
echo ========================================
echo Windows one-click setup complete
echo ========================================
echo Project root : %ROOT_DIR%
echo Preset       : %PRESET%
echo.
echo Optional:
echo   setup_windows_oneclick.bat --run-wsl-setup
echo   setup_windows_oneclick.bat --open-vs
echo ========================================
exit /b 0

:prepare_repo
set "TARGET=%~1"
set "REPO=%~2"

if exist "%TARGET%\.git" (
  echo [INFO] Found repo: %TARGET%
  exit /b 0
)

if exist "%ROOT_DIR%\.gitmodules" (
  for /f "tokens=1,2" %%A in ('git config -f "%ROOT_DIR%\.gitmodules" --get-regexp "^submodule\..*\.path$" 2^>nul') do (
    if /I "%%B"=="external/inipp_repo" if /I "%TARGET%"=="%ROOT_DIR%\external\inipp_repo" (
      echo [INFO] Initializing submodule: external\inipp_repo
      git -C "%ROOT_DIR%" submodule update --init --recursive -- external/inipp_repo
      exit /b !ERRORLEVEL!
    )
  )
)

if exist "%TARGET%" (
  echo [WARN] %TARGET% exists and is not a git repo; leaving as-is
  exit /b 0
)

echo [INFO] Cloning %REPO% to %TARGET%
git clone --depth 1 "%REPO%" "%TARGET%"
exit /b %ERRORLEVEL%

:run_wsl_setup
where wsl >nul 2>nul || (echo [ERROR] wsl not found & exit /b 1)

for /f "usebackq delims=" %%I in (`wsl wslpath "%ROOT_DIR%"`) do set "WSL_ROOT_DIR=%%I"
if "%WSL_ROOT_DIR%"=="" (
  echo [ERROR] Failed to convert Windows path to WSL path
  exit /b 1
)

set "WSL_CMD=cd '%WSL_ROOT_DIR%' && chmod +x ./dev_env.sh && ./dev_env.sh"
if not "%WSL_DISTRO%"=="" (
  echo [INFO] Running WSL setup on distro: %WSL_DISTRO%
  wsl -d "%WSL_DISTRO%" bash -lc "%WSL_CMD%"
) else (
  echo [INFO] Running WSL setup on default distro
  wsl bash -lc "%WSL_CMD%"
)
exit /b %ERRORLEVEL%

:usage
echo Usage: setup_windows_oneclick.bat [options]
echo.
echo Options:
echo   --build-type Debug^|Release
echo   --preset ^<name^>
echo   --no-configure
echo   --no-build
echo   --force-clean
echo   --jobs ^<N^>
echo   --open-vs
echo   --run-wsl-setup
echo   --wsl-distro ^<name^>
exit /b 0
