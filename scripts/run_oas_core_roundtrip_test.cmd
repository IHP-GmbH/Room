@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "CORE_SOURCE_DIR=%ROOT%"
cd /d "%ROOT%"
set "BUILD=%ROOT%\build"
set "INPUT_OAS="
set "INPUT_GDS=%ROOT%\examples\gds_to_core\data\sg13g2_stdcell.gds"
set "WORK_DIR=%BUILD%\tests\oas_core"
set "ROUND_OAS=%WORK_DIR%\roundtrip.oas"
set "SOURCE_OAS=%WORK_DIR%\source.oas"

if not "%~1"=="" set "BUILD=%~1"
if not "%~2"=="" (
  if /I "%~x2"==".oas" (
    set "INPUT_OAS=%~2"
    if not "%~3"=="" set "INPUT_GDS=%~3"
  ) else (
    set "INPUT_GDS=%~2"
    if not "%~3"=="" set "INPUT_OAS=%~3"
  )
)

if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"

set "ROUNDTRIP=%BUILD%\oas_core_roundtrip.exe"
if not exist "%ROUNDTRIP%" set "ROUNDTRIP=%BUILD%\oas_core_roundtrip"
if not exist "%ROUNDTRIP%" (
  echo Missing oas_core_roundtrip. Run: cmake --build build --target oas_core_roundtrip >&2
  exit /b 1
)

set "KLAYOUT="
if defined KLAYOUT_EXE if exist "%KLAYOUT_EXE%" set "KLAYOUT=%KLAYOUT_EXE%"
if not defined KLAYOUT if exist "%APPDATA%\KLayout\klayout_app.exe" set "KLAYOUT=%APPDATA%\KLayout\klayout_app.exe"
if not defined KLAYOUT if exist "%LOCALAPPDATA%\KLayout\klayout_app.exe" set "KLAYOUT=%LOCALAPPDATA%\KLayout\klayout_app.exe"
if not defined KLAYOUT if exist "%ProgramFiles%\KLayout\klayout_app.exe" set "KLAYOUT=%ProgramFiles%\KLayout\klayout_app.exe"
if not defined KLAYOUT if exist "%ProgramFiles(x86)%\KLayout\klayout_app.exe" set "KLAYOUT=%ProgramFiles(x86)%\KLayout\klayout_app.exe"
where klayout_app.exe >nul 2>&1 && if not defined KLAYOUT for /f "delims=" %%K in ('where klayout_app.exe 2^>nul') do set "KLAYOUT=%%K" & goto :have_klayout
:have_klayout
if not defined KLAYOUT (
  echo KLayout not found. Set KLAYOUT_EXE or add klayout to PATH. >&2
  exit /b 1
)

if defined INPUT_OAS (
  set "SOURCE_OAS=%INPUT_OAS%"
) else if exist "%INPUT_GDS%" (
  echo === Prepare source OAS from GDS ===
  "%KLAYOUT%" -b -rd in=%INPUT_GDS% -rd out=%SOURCE_OAS% -r "%ROOT%\scripts\klayout_convert_layout.drc"
  echo.
) else (
  echo No input OAS or GDS found. >&2
  exit /b 1
)

if not exist "%SOURCE_OAS%" (
  echo Input OAS not found: %SOURCE_OAS% >&2
  exit /b 1
)

echo === OAS -^> CORE -^> OAS ===
"%ROUNDTRIP%" "%SOURCE_OAS%" "%ROUND_OAS%"
if errorlevel 1 exit /b 1

echo.
echo === Native round-trip verify (in oas_core_roundtrip) ===
echo PASSED if tool reported "Native round-trip OK"
echo.
echo === KLayout DRC XOR compare ===
echo Using KLayout: %KLAYOUT%
"%KLAYOUT%" -b -rd oas1=%SOURCE_OAS% -rd oas2=%ROUND_OAS% -rd skip_text=1 -r "%ROOT%\scripts\klayout_compare_oas.drc"
echo Done. If there is no 'Invalid record' above, KLayout can read roundtrip.oas.
exit /b 0
