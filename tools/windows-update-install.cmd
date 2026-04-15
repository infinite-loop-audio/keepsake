@echo off
setlocal

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"

set "CONFIG=Debug"
set "BUILD_DIR=%REPO%\build-win"
set "GENERATOR=Visual Studio 17 2022"
set "PLATFORM=x64"
set "SOURCE_CLAP=%BUILD_DIR%\%CONFIG%\keepsake.clap"
set "SOURCE_BRIDGE=%BUILD_DIR%\%CONFIG%\keepsake-bridge.exe"
set "CLAP_TARGET=%CommonProgramFiles%\CLAP\keepsake.clap"
set "BRIDGE_TARGET=%CommonProgramFiles%\CLAP\keepsake-bridge.exe"
set "DEBUG_LOG=%TEMP%\keepsake-debug.log"
set "INSTALL_HELPER=%REPO%\tools\windows-install-built.ps1"
set "INSTALL_TASK_RUNNER=%REPO%\tools\windows-run-install-task.ps1"
set "INSTALL_TASK_NAME=KeepsakeInstallBuilt"

echo [keepsake] repo=%REPO%
cd /d "%REPO%" || exit /b 1

git pull --ff-only || exit /b 1
cmake -S "%REPO%" -B "%BUILD_DIR%" -G "%GENERATOR%" -A %PLATFORM% || exit /b 1
cmake --build "%BUILD_DIR%" --config %CONFIG% || exit /b 1
if exist "%REPO%\tools\windows-build-bridge32.cmd" (
  call "%REPO%\tools\windows-build-bridge32.cmd"
)

taskkill /IM reaper.exe /F >NUL 2>&1
taskkill /IM keepsake-bridge.exe /F >NUL 2>&1

schtasks /Query /TN "%INSTALL_TASK_NAME%" >NUL 2>&1
if %ERRORLEVEL% EQU 0 (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%INSTALL_TASK_RUNNER%" -TaskName "%INSTALL_TASK_NAME%" || exit /b 1
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%INSTALL_HELPER%" -SourceBundle "%SOURCE_CLAP%" -TargetBundle "%CLAP_TARGET%" || exit /b 1
)
del /Q "%DEBUG_LOG%" 2>NUL

echo [keepsake] source:
dir "%SOURCE_CLAP%" | findstr /R /C:"keepsake.clap"
echo [keepsake] installed:
dir "%CLAP_TARGET%" | findstr /R /C:"keepsake.clap"
echo [keepsake] source hash:
certutil -hashfile "%SOURCE_CLAP%" SHA256 | findstr /R /V /C:"hash of file" /C:"CertUtil:"
echo [keepsake] installed hash:
certutil -hashfile "%CLAP_TARGET%" SHA256 | findstr /R /V /C:"hash of file" /C:"CertUtil:"
echo [keepsake] source bridge:
dir "%SOURCE_BRIDGE%" | findstr /R /C:"keepsake-bridge.exe"
echo [keepsake] installed bridge:
dir "%BRIDGE_TARGET%" | findstr /R /C:"keepsake-bridge.exe"
echo [keepsake] source bridge hash:
certutil -hashfile "%SOURCE_BRIDGE%" SHA256 | findstr /R /V /C:"hash of file" /C:"CertUtil:"
echo [keepsake] installed bridge hash:
certutil -hashfile "%BRIDGE_TARGET%" SHA256 | findstr /R /V /C:"hash of file" /C:"CertUtil:"
echo [keepsake] debug_log=%DEBUG_LOG%

exit /b 0
