@echo off
setlocal

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"

set "CONFIG=Debug"
set "BUILD_DIR=%REPO%\build-win32"
set "GENERATOR=Visual Studio 17 2022"
set "PLATFORM=Win32"
set "X64_DIR=%REPO%\build-win\%CONFIG%"

echo [keepsake-x86] repo=%REPO%
cd /d "%REPO%" || exit /b 1

cmake -S "%REPO%" -B "%BUILD_DIR%" -G "%GENERATOR%" -A %PLATFORM% || exit /b 1
cmake --build "%BUILD_DIR%" --config %CONFIG% --target keepsake-bridge || exit /b 1

if exist "%BUILD_DIR%\%CONFIG%\keepsake-bridge-32.exe" (
  if not exist "%X64_DIR%" mkdir "%X64_DIR%"
  copy /Y "%BUILD_DIR%\%CONFIG%\keepsake-bridge-32.exe" "%X64_DIR%\keepsake-bridge-32.exe" >NUL || exit /b 1
)

echo [keepsake-x86] built %BUILD_DIR%\%CONFIG%\keepsake-bridge-32.exe
exit /b 0
