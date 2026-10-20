@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\ci\setup_windows_build.ps1" %*
set "SETUP_EXIT=%ERRORLEVEL%"
echo.
if not "%SETUP_EXIT%"=="0" echo Setup failed. Keep the log and report the error; no automatic restart was requested.
pause
exit /b %SETUP_EXIT%
