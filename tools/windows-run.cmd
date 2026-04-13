@echo off
setlocal

if "%~1"=="" goto :usage

set "SCRIPT_DIR=%~dp0"
set "CMD=%~1"
shift

if /I "%CMD%"=="update-install" (
  call "%SCRIPT_DIR%windows-update-install.cmd" %*
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="dump-debug" (
  call "%SCRIPT_DIR%windows-dump-debug.cmd" %*
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="smoke-apc" (
  call "%SCRIPT_DIR%windows-reaper-smoke.cmd" keepsake.vst2.41706364 --vst-path "C:\Program Files\Ample Sound\APC.dll" %*
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="smoke-serum" (
  call "%SCRIPT_DIR%windows-reaper-smoke.cmd" keepsake.vst2.58667358 --vst-path "%CommonProgramFiles%\VST2\Serum_x64.dll" %*
  exit /b %ERRORLEVEL%
)

echo [keepsake] unknown windows-run verb: %CMD%
goto :usage

:usage
echo usage: tools\windows-run.cmd ^<verb^> [extra args]
echo verbs:
echo   update-install
echo   dump-debug
echo   smoke-apc [extra reaper-smoke args]
echo   smoke-serum [extra reaper-smoke args]
exit /b 1
