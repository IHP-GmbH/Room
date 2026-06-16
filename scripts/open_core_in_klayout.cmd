@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "CORE_EXE=%ROOT%\build\core_to_gds.exe"
set "KLAYOUT=%KLAYOUT_BIN%"

if not defined KLAYOUT (
  if exist "%ROOT%\..\KLayout\bin-release\klayout.exe" (
    set "KLAYOUT=%ROOT%\..\KLayout\bin-release\klayout.exe"
  ) else if exist "%LOCALAPPDATA%\KLayout\klayout.exe" (
    set "KLAYOUT=%LOCALAPPDATA%\KLayout\klayout.exe"
  ) else (
    set "KLAYOUT=klayout.exe"
  )
)

set "CORE_FILE=%~1"
if "%CORE_FILE%"=="" (
  echo Usage: %~nx0 path\to\file.core
  exit /b 1
)

if not exist "%CORE_EXE%" (
  echo Build core_to_gds first: cmake --build build --target core_to_gds
  exit /b 2
)

set "GDS_FILE=%TEMP%\core_preview_%RANDOM%.gds"
echo Converting %CORE_FILE% ...
"%CORE_EXE%" "%CORE_FILE%" "%GDS_FILE%"
if errorlevel 1 exit /b 3

echo Opening in KLayout: %GDS_FILE%
start "" "%KLAYOUT%" "%GDS_FILE%"
