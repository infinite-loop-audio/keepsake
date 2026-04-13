@echo off
setlocal

if "%~1"=="" (
  echo usage: tools\windows-reaper-smoke.cmd ^<plugin-id^> --vst-path ^<path^> [extra args]
  exit /b 1
)

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"

powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%\tools\reaper-smoke.ps1" %*
exit /b %ERRORLEVEL%
