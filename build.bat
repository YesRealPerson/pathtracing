@echo off
rem ---------------------------------------------------------------------------
rem Configure (if needed) and build the path tracer with Ninja + CUDA.
rem
rem Usage (from any directory, e.g. inside build\ as ..\build.bat):
rem   build.bat            Release build into build\
rem   build.bat debug      Debug build (with CUDA -G) into build-debug\
rem   build.bat clean      Delete build\ and do a fresh Release build
rem   build.bat reconfig   Re-run CMake configure from scratch (--fresh), then build
rem ---------------------------------------------------------------------------
setlocal

set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"
rem Your vcpkg checkout (has freetype installed). Not named VCPKG_ROOT on purpose:
rem vcvars64.bat overwrites VCPKG_ROOT with Visual Studio's bundled vcpkg.
set "PT_VCPKG=C:\Users\Stephen\Desktop\184\vcpkg"

set "MODE=%~1"
set "BUILD_DIR=%SRC%\build"
set "CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Release"
if /i "%MODE%"=="debug" (
  set "BUILD_DIR=%SRC%\build-debug"
  set "CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Debug -DBUILD_DEBUG=ON"
)

rem --- Load the MSVC environment if cl.exe isn't already on PATH (nvcc needs it)
where cl.exe >nul 2>&1
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo [build] vswhere.exe not found. Install Visual Studio 2022 with C++ tools.
    exit /b 1
  )
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR (
  echo [build] Loading MSVC environment from "%VSDIR%"
  call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
  if errorlevel 1 (
    echo [build] Failed to load vcvars64.bat
    exit /b 1
  )
)

if /i "%MODE%"=="clean" (
  echo [build] Removing "%BUILD_DIR%"
  rem Step out first in case we were launched from inside the build folder.
  cd /d "%SRC%"
  if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

rem --- Configure on first run (or when asked)
set "NEED_CONFIGURE="
set "FRESH="
if not exist "%BUILD_DIR%\build.ninja" set "NEED_CONFIGURE=1"
if /i "%MODE%"=="reconfig" (
  set "NEED_CONFIGURE=1"
  set "FRESH=--fresh"
)
if defined NEED_CONFIGURE (
  echo [build] Configuring into "%BUILD_DIR%"
  cmake %FRESH% -S "%SRC%" -B "%BUILD_DIR%" -G Ninja %CMAKE_ARGS% ^
    -DCMAKE_TOOLCHAIN_FILE="%PT_VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
  if errorlevel 1 (
    echo [build] CMake configure failed.
    exit /b 1
  )
)

echo [build] Building
cmake --build "%BUILD_DIR%"
if errorlevel 1 (
  echo [build] Build FAILED.
  exit /b 1
)

echo [build] Done: "%BUILD_DIR%\pathtracer.exe"
endlocal
