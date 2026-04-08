@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "BUILD_DIR=build"
set "TEST_TMPDIR=%~dp0.tmp\pytest"

if not exist "%TEST_TMPDIR%" mkdir "%TEST_TMPDIR%"

set "VIGIL_TEST_TMPDIR=%TEST_TMPDIR%"
set "TMPDIR=%TEST_TMPDIR%"
set "TEMP=%TEST_TMPDIR%"
set "TMP=%TEST_TMPDIR%"
if defined PYTHONPATH (
  set "PYTHONPATH=%CD%;%PYTHONPATH%"
) else (
  set "PYTHONPATH=%CD%"
)

cmake -S . -B "%BUILD_DIR%" ^
  -DVIGIL_BUILD_TESTS=ON ^
  -DVIGIL_PLUGIN_SDL=ON ^
  -DVIGIL_PLUGIN_GUI=ON ^
  -DVIGIL_PLUGIN_AUDIO=ON ^
  -DVIGIL_PLUGIN_SYSQUERY=ON ^
  -DVIGIL_PLUGIN_TILED=ON ^
  -DVIGIL_STDLIB_FFI=ON ^
  -DVIGIL_STDLIB_FS=ON ^
  -DVIGIL_STDLIB_HTTP=ON ^
  -DVIGIL_STDLIB_NET=ON ^
  -DVIGIL_STDLIB_READLINE=ON ^
  -DVIGIL_STDLIB_THREAD=ON ^
  -DVIGIL_STDLIB_TIME=ON
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b %errorlevel%

set "VIGIL_BIN=%CD%\%BUILD_DIR%\RELEASE\vigil.exe"
if not exist "%VIGIL_BIN%" set "VIGIL_BIN=%CD%\%BUILD_DIR%\Release\vigil.exe"
if not exist "%VIGIL_BIN%" set "VIGIL_BIN=%CD%\%BUILD_DIR%\vigil.exe"

ctest --test-dir "%BUILD_DIR%" --output-on-failure -C Release
if errorlevel 1 exit /b %errorlevel%

python -m unittest discover -s integration_tests -p "test_*.py" -v
if errorlevel 1 exit /b %errorlevel%
