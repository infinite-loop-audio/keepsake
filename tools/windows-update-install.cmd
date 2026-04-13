@echo off
setlocal

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"

set "CONFIG=Debug"
set "BUILD_DIR=%REPO%\build-win"
set "CLAP_TARGET=%CommonProgramFiles%\CLAP\keepsake.clap"
set "BRIDGE_TARGET=%CommonProgramFiles%\CLAP\keepsake-bridge.exe"
set "DEBUG_LOG=%TEMP%\keepsake-debug.log"

echo [keepsake] repo=%REPO%
cd /d "%REPO%" || exit /b 1

git pull --ff-only || exit /b 1
cmake --build "%BUILD_DIR%" --config %CONFIG% || exit /b 1

taskkill /IM reaper.exe /F >NUL 2>&1
taskkill /IM keepsake-bridge.exe /F >NUL 2>&1

copy /Y "%BUILD_DIR%\%CONFIG%\keepsake.clap" "%CLAP_TARGET%" >NUL || exit /b 1
copy /Y "%BUILD_DIR%\%CONFIG%\keepsake-bridge.exe" "%BRIDGE_TARGET%" >NUL || exit /b 1
del /Q "%DEBUG_LOG%" 2>NUL

echo [keepsake] installed:
dir "%CLAP_TARGET%" | findstr /R /C:"keepsake.clap"
echo [keepsake] debug_log=%DEBUG_LOG%

exit /b 0
