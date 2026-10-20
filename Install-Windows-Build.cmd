@echo off
call "%~dp0Setup-Windows-Build.cmd" -Install %*
exit /b %ERRORLEVEL%
