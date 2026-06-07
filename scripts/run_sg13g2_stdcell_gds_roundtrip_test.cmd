@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "INPUT_GDS=%ROOT%\examples\gds_to_core\data\sg13g2_stdcell.gds"
set "WORK_DIR=%BUILD%\tests\sg13g2_stdcell"
set "ROUND_GDS=%WORK_DIR%\roundtrip.gds"

if not "%~2"=="" set "INPUT_GDS=%~2"
if not "%~1"=="" set "BUILD=%~1"

if not exist "%INPUT_GDS%" (
  echo Input GDS not found: %INPUT_GDS% >&2
  exit /b 1
)

if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"

set "ROUNDTRIP=%BUILD%\gds_core_roundtrip.exe"
if not exist "%ROUNDTRIP%" set "ROUNDTRIP=%BUILD%\gds_core_roundtrip"
if not exist "%ROUNDTRIP%" (
  echo Missing gds_core_roundtrip. Run: cmake --build build --target gds_core_roundtrip >&2
  exit /b 1
)

set "KLAYOUT="
if defined KLAYOUT_EXE if exist "%KLAYOUT_EXE%" set "KLAYOUT=%KLAYOUT_EXE%"
if not defined KLAYOUT if exist "%LOCALAPPDATA%\KLayout\klayout_app.exe" set "KLAYOUT=%LOCALAPPDATA%\KLayout\klayout_app.exe"
if not defined KLAYOUT if exist "%APPDATA%\KLayout\klayout_app.exe" set "KLAYOUT=%APPDATA%\KLayout\klayout_app.exe"
where klayout >nul 2>&1 && if not defined KLAYOUT for /f "delims=" %%K in ('where klayout 2^>nul') do set "KLAYOUT=%%K" & goto :have_klayout
where klayout_app.exe >nul 2>&1 && if not defined KLAYOUT for /f "delims=" %%K in ('where klayout_app.exe 2^>nul') do set "KLAYOUT=%%K" & goto :have_klayout
:have_klayout
if not defined KLAYOUT (
  echo KLayout not found. Set KLAYOUT_EXE or add klayout to PATH. >&2
  exit /b 1
)

echo === sg13g2_stdcell.gds: GDS -^> CORE -^> GDS ===
"%ROUNDTRIP%" "%INPUT_GDS%" "%ROUND_GDS%"
if errorlevel 1 exit /b 1

echo.
echo === KLayout DRC XOR compare ===
"%KLAYOUT%" -b -rd gds1=%INPUT_GDS% -rd gds2=%ROUND_GDS% -r "%ROOT%\scripts\klayout_compare_gds.drc"
exit /b %ERRORLEVEL%
