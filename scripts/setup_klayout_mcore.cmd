@echo off
setlocal

set "ROOT=%~dp0.."
if not "%~1"=="" (
  set "KLAYOUT=%~1"
) else (
  set "KLAYOUT=%ROOT%\..\KLayout"
)

set "STREAMERS=%KLAYOUT%\src\plugins\streamers"
set "MCORE=%ROOT%\integrations\klayout\mcore"
set "LINK=%STREAMERS%\mcore"

if not exist "%KLAYOUT%\src" (
  echo KLayout not found at %KLAYOUT%
  echo Usage: %~nx0 [path\to\KLayout]
  echo Example: %~nx0 C:\dev\KLayout
  exit /b 1
)

if exist "%LINK%" (
  echo Junction already exists: %LINK%
) else (
  mklink /J "%LINK%" "%MCORE%"
  if errorlevel 1 exit /b 1
  echo Linked %LINK% -^> %MCORE%
)

if not exist "%MCORE%\db_plugin\local.pri" (
  copy /Y "%MCORE%\db_plugin\local.pri.example" "%MCORE%\db_plugin\local.pri"
  echo Created local.pri - edit COMMONDB_ROOT / KLAYOUT_SRC if needed
)

(
  echo COMMONDB_ROOT = %ROOT:\=/%
  echo KLAYOUT_SRC = %KLAYOUT%/src
) > "%MCORE%\db_plugin\local.pri"

echo.
echo Wrote %MCORE%\db_plugin\local.pri
echo Next: rebuild KLayout from %KLAYOUT%
echo   build.bat -j 4
echo Then open a .core file in bin-release\klayout.exe
