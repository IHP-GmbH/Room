@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build"
set "INPUT_GDS=%ROOT%\testdata\sample.gds"
set "WORK_DIR=%BUILD%\tests\sample_gds"
set "ROUND_GDS=%WORK_DIR%\roundtrip.gds"

if not "%~2"=="" set "INPUT_GDS=%~2"
if not "%~1"=="" set "BUILD=%~1"

if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"

set "SAMPLE_GEN=%BUILD%\make_sample_gds.exe"
if not exist "%SAMPLE_GEN%" set "SAMPLE_GEN=%BUILD%\make_sample_gds"
if exist "%SAMPLE_GEN%" (
  echo === Generate testdata/sample.gds ===
  "%SAMPLE_GEN%" "%INPUT_GDS%"
  echo.
)

if not exist "%INPUT_GDS%" (
  echo Input GDS not found: %INPUT_GDS% >&2
  exit /b 1
)

set "ROUNDTRIP=%BUILD%\gds_room_roundtrip.exe"
if not exist "%ROUNDTRIP%" set "ROUNDTRIP=%BUILD%\gds_room_roundtrip"
if not exist "%ROUNDTRIP%" (
  echo Missing gds_room_roundtrip. Run: cmake --build build --target gds_room_roundtrip >&2
  exit /b 1
)

echo === sample.gds: GDS -^> ROOM -^> GDS ===
"%ROUNDTRIP%" "%INPUT_GDS%" "%ROUND_GDS%"
exit /b %ERRORLEVEL%
