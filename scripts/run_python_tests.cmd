@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

set "MINGW_BIN=C:\msys64\mingw64\bin"
if not exist "%MINGW_BIN%\g++.exe" (
  echo ERROR: MinGW64 g++ not found at %MINGW_BIN%
  echo Adjust MINGW_BIN in scripts\run_python_tests.cmd
  exit /b 1
)

set "PATH=%MINGW_BIN%;%PATH%"
where cmake >nul 2>&1
if errorlevel 1 (
  if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" set "PATH=C:\Qt\Tools\CMake_64\bin;%PATH%"
)

if not exist build-python\python\libroom_c.dll (
  echo Configuring build-python...
  cmake -S . -B build-python -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="%MINGW_BIN%\g++.exe" ^
    -DCMAKE_MAKE_PROGRAM="%MINGW_BIN%\mingw32-make.exe" ^
    -DROOM_BUILD_PYTHON=ON ^
    -DROOM_BUILD_EXAMPLES=OFF ^
    -DROOM_BUILD_TESTS=OFF
  if errorlevel 1 exit /b 1
)

echo Building room_c...
cmake --build build-python --target room_c -j
if errorlevel 1 exit /b 1

set "ROOM_C_DLL=%CD%\build-python\python\libroom_c.dll"
set "PYTHONPATH=%CD%\python"
set "PATH=%MINGW_BIN%;%PATH%"

where python >nul 2>&1
if errorlevel 1 (
  echo ERROR: python not on PATH
  exit /b 1
)

python -m pip install -q pytest
python -m pytest python\tests -q --tb=short
exit /b %ERRORLEVEL%
