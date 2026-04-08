@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "BUILD_DIR=build"

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
