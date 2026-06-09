@echo off
setlocal enabledelayedexpansion

for %%I in ("%~dp0..") do set ROOT_DIR=%%~fI
if not defined BUILD_DIR set BUILD_DIR=%ROOT_DIR%\build-coverage
if not defined REPORT_NAME set REPORT_NAME=coverage.html

echo Configuring coverage build in "%BUILD_DIR%"...
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCORE_ENABLE_COVERAGE=ON ^
    -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw810_64/bin/gcc.exe ^
    -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw810_64/bin/g++.exe ^
    -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/mingw810_64/bin/mingw32-make.exe
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" -j4
if errorlevel 1 exit /b 1

echo Cleaning old coverage data in "%BUILD_DIR%"...
del /s /q "%BUILD_DIR%\*.gcda" > nul 2>&1
del /s /q "%BUILD_DIR%\*.gcov" > nul 2>&1

set "PATH=C:\Qt\Tools\mingw810_64\bin;%PATH%"
set "CORE_SOURCE_DIR=%ROOT_DIR%"

echo Running tests...
pushd "%ROOT_DIR%"
ctest --test-dir "%BUILD_DIR%" --output-on-failure
set TEST_EXIT=%ERRORLEVEL%
popd

if not "%TEST_EXIT%"=="0" (
    echo Tests reported %TEST_EXIT% failure^(s^).
) else (
    echo All tests passed.
)

echo Generating coverage report...

pushd "%ROOT_DIR%"

python -m gcovr -j 1 ^
    -r "%ROOT_DIR%" ^
    --object-directory "%BUILD_DIR%" ^
    --merge-mode-functions=merge-use-line-min ^
    --gcov-ignore-errors=all ^
    --filter "%ROOT_DIR%/src/.*" ^
    --filter "%ROOT_DIR%/utils/.*" ^
    --exclude "%ROOT_DIR%/tests/.*" ^
    --exclude ".*/build/.*" ^
    --exclude ".*/build-coverage/.*" ^
    --exclude ".*/third_party/.*" ^
    --exclude ".*/generated/.*" ^
    --exclude ".*/tools/.*" ^
    --exclude ".*/examples/.*" ^
    --html-details ^
    -o "%REPORT_NAME%" ^
    --print-summary

set GCOVR_EXIT=%ERRORLEVEL%

if exist "%REPORT_NAME%" (
    start "" "%REPORT_NAME%"
    del /s /q "%ROOT_DIR%\*.gcov" > nul 2>&1
) else (
    echo Error: %REPORT_NAME% not generated.
)

popd

echo.
echo Summary: test failures=%TEST_EXIT%, gcovr exit=%GCOVR_EXIT%

if not "%GCOVR_EXIT%"=="0" (
    echo Error: gcovr failed.
    exit /b %GCOVR_EXIT%
)

if not "%TEST_EXIT%"=="0" (
    echo Error: one or more tests failed.
    exit /b 1
)

exit /b 0
