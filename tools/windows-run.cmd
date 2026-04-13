@echo off
setlocal

if "%~1"=="" goto :usage

set "SCRIPT_DIR=%~dp0"
set "CMD=%~1"
set "EXTRA=%~2 %~3 %~4 %~5 %~6 %~7 %~8 %~9"

if /I "%CMD%"=="update-install" (
  call "%SCRIPT_DIR%windows-update-install.cmd" %EXTRA%
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="dump-debug" (
  call "%SCRIPT_DIR%windows-dump-debug.cmd" %EXTRA%
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="smoke-apc" (
  call "%SCRIPT_DIR%windows-reaper-smoke.cmd" keepsake.vst2.41706364 -VstPath "C:\Program Files\Ample Sound\APC.dll" %EXTRA%
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="smoke-apc-embed" (
  call "%SCRIPT_DIR%windows-reaper-smoke.cmd" keepsake.vst2.41706364 -VstPath "C:\Program Files\Ample Sound\APC.dll" -UseDefaultConfig -SyncDefaultInstall -OpenUi -KeepTempOnFailure -RequireDebugPattern "build=","gui_open_editor_embedded_impl enter","EDITOR_SET_PARENT" %EXTRA%
  exit /b %ERRORLEVEL%
)

if /I "%CMD%"=="smoke-serum" (
  call "%SCRIPT_DIR%windows-reaper-smoke.cmd" keepsake.vst2.58667358 -VstPath "%CommonProgramFiles%\VST2\Serum_x64.dll" %EXTRA%
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
echo   smoke-apc-embed [extra reaper-smoke args]
echo   smoke-serum [extra reaper-smoke args]
exit /b 1
