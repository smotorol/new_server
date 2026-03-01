@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo =========================================
echo  Auto Setup: Git init + vcpkg submodule
echo  + repair broken state + bootstrap
echo  Working dir: %CD%
echo =========================================
echo.

REM -----------------------
REM 0) Check git
REM -----------------------
where git >nul 2>nul
if errorlevel 1 (
  echo ERROR: git not found. Install Git for Windows first.
  pause
  exit /b 1
)

REM -----------------------
REM 1) Ensure git repo
REM -----------------------
if not exist ".git" (
  echo [1/6] git init
  git init
  if errorlevel 1 (
    echo ERROR: git init failed.
    pause
    exit /b 1
  )
) else (
  echo [1/6] git repo already exists.
)

REM -----------------------
REM 2) Prepare folders
REM -----------------------
echo [2/6] Ensure folders...
if not exist "external" mkdir "external" >nul 2>nul

REM -----------------------
REM 3) Detect broken vcpkg state
REM -----------------------
echo [3/6] Detect/Repair vcpkg submodule state...

REM Case: external\vcpkg exists but missing bootstrap file => broken/empty
if exist "external\vcpkg" (
  if not exist "external\vcpkg\bootstrap-vcpkg.bat" (
    echo - Detected broken external\vcpkg (missing bootstrap-vcpkg.bat)
    echo - Cleaning broken state...
    rmdir /s /q "external\vcpkg" 2>nul
    rmdir /s /q ".git\modules\external\vcpkg" 2>nul

    REM Remove submodule section if exists (ignore errors)
    git config --remove-section submodule.external/vcpkg 2>nul
  )
)

REM Also clean invalid .gitmodules entry if it exists but folder missing
if exist ".gitmodules" (
  findstr /C:"external/vcpkg" ".gitmodules" >nul 2>nul
  if not errorlevel 1 (
    if not exist "external\vcpkg" (
      echo - .gitmodules has vcpkg entry but folder missing. We'll re-add.
    )
  )
)

REM -----------------------
REM 4) Add submodule if needed
REM -----------------------
echo [4/6] Ensure vcpkg submodule exists...

if not exist "external\vcpkg\.git" (
  REM If not registered, add it
  REM (git submodule add fails if already registered, so we allow errors then continue update)
  if not exist "external\vcpkg" (
    echo - Adding vcpkg submodule...
    git submodule add https://github.com/microsoft/vcpkg external/vcpkg 2>nul
  )
)

REM inipp submodule
if not exist "external\inipp_repo" (
  echo - Adding inipp submodule...
  git submodule add https://github.com/mcmtroffaes/inipp external/inipp_repo 2>nul
)

REM -----------------------
REM 5) Update/init submodules (guarantees checkout)
REM -----------------------
echo [5/6] git submodule update --init --recursive
git submodule update --init --recursive
if errorlevel 1 (
  echo ERROR: submodule update failed.
  echo - Check network access to GitHub.
  pause
  exit /b 1
)

REM -----------------------
REM 6) Bootstrap vcpkg
REM -----------------------
echo [6/6] Bootstrapping vcpkg...

if not exist "external\vcpkg\bootstrap-vcpkg.bat" (
  echo ERROR: bootstrap-vcpkg.bat not found even after submodule update.
  echo - Something is still wrong with external\vcpkg contents.
  echo - Please check: dir external\vcpkg
  pause
  exit /b 1
)

call "external\vcpkg\bootstrap-vcpkg.bat"
if errorlevel 1 (
  echo ERROR: vcpkg bootstrap failed.
  pause
  exit /b 1
)

if not exist "external\vcpkg\vcpkg.exe" (
  echo ERROR: vcpkg.exe not created. bootstrap may have failed silently.
  pause
  exit /b 1
)

echo.
echo SUCCESS!
echo - vcpkg: external\vcpkg\vcpkg.exe
echo.
pause
exit /b 0
