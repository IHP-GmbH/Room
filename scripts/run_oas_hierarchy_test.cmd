@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "INPUT_GDS=%ROOT%\examples\gds_to_room\data\sg13g2_stdcell.gds"
set "WORK_DIR=%BUILD%\tests\oas"
set "MINIMAL_OAS=%WORK_DIR%\minimal.oas"
set "CONVERTED_OAS=%WORK_DIR%\sg13g2_stdcell.oas"

if not "%~2"=="" set "INPUT_GDS=%~2"
if not "%~1"=="" set "BUILD=%~1"
if not "%~3"=="" set "PATH=%~3;%PATH%"

if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"

set "TOOL=%BUILD%\oas_hierarchy.exe"
if not exist "%TOOL%" set "TOOL=%BUILD%\oas_hierarchy"
if not exist "%TOOL%" (
  echo Missing oas_hierarchy. Run: cmake --build build --target oas_hierarchy >&2
  exit /b 1
)

echo === OAS smoke: create + read ===
"%TOOL%" smoke "%MINIMAL_OAS%" TOP
if errorlevel 1 exit /b 1

if not exist "%INPUT_GDS%" (
  echo Input GDS not found: %INPUT_GDS% - skipping sg13g2 OAS read >&2
  exit /b 0
)

set "KLAYOUT="
if defined KLAYOUT_EXE if exist "%KLAYOUT_EXE%" set "KLAYOUT=%KLAYOUT_EXE%"
if not defined KLAYOUT if exist "%LOCALAPPDATA%\KLayout\klayout_app.exe" set "KLAYOUT=%LOCALAPPDATA%\KLayout\klayout_app.exe"
if not defined KLAYOUT if exist "%APPDATA%\KLayout\klayout_app.exe" set "KLAYOUT=%APPDATA%\KLayout\klayout_app.exe"
where klayout >nul 2>&1 && if not defined KLAYOUT for /f "delims=" %%K in ('where klayout 2^>nul') do set "KLAYOUT=%%K" & goto :have_klayout
where klayout_app.exe >nul 2>&1 && if not defined KLAYOUT for /f "delims=" %%K in ('where klayout_app.exe 2^>nul') do set "KLAYOUT=%%K" & goto :have_klayout
:have_klayout
if not defined KLAYOUT (
  echo KLayout not found; skipping GDS-^>OAS conversion and sg13g2 hierarchy read >&2
  exit /b 0
)

echo.
echo === Convert GDS to OAS (KLayout) ===
"%KLAYOUT%" -b -rd gds=%INPUT_GDS% -rd oas=%CONVERTED_OAS% -r "%ROOT%\scripts\klayout_gds_to_oas.drc"
if errorlevel 1 exit /b 1

echo.
echo === OAS hierarchy read: sg13g2_stdcell ===
"%TOOL%" read "%CONVERTED_OAS%"
exit /b %ERRORLEVEL%
