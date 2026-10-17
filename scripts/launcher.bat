@echo off
set "ZIP_FILE=MultiProtocolParser_v1.2.1.zip"
set "EXE_FILE=MultiProtocolParser.exe"

echo Extracting application...
tar -xf "%ZIP_FILE%"
if %ERRORLEVEL% NEQ 0 (
    echo Extraction failed!
    pause
    exit /b 1
)

echo Launching application...
start /wait "" "%EXE_FILE%"
