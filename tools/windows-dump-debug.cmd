@echo off
setlocal

set "DEBUG_LOG=%TEMP%\keepsake-debug.log"

if not exist "%DEBUG_LOG%" (
  echo [keepsake] missing debug log: %DEBUG_LOG%
  exit /b 1
)

type "%DEBUG_LOG%"
exit /b 0
