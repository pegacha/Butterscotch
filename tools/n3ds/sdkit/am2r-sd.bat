@echo off
rem AM2R 3DS - SD card builder for Windows: runs am2r-sd.sh in WSL (Windows Subsystem for Linux), offering to
rem install WSL first if it isn't there. am2r-sd.sh is downloaded next to this file if it is missing.
setlocal
title AM2R 3DS - SD card builder
set "REPO=pegacha/Butterscotch"
cd /d "%~dp0"

where wsl.exe >nul 2>nul || goto :nowsl
wsl.exe -e true >nul 2>nul || goto :nowsl

if not exist "am2r-sd.sh" (
    echo Downloading am2r-sd.sh ...
    powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/%REPO%/releases/latest/download/am2r-sd.sh' -OutFile 'am2r-sd.sh'"
    if not exist "am2r-sd.sh" (
        echo Could not download am2r-sd.sh. Check the internet connection and try again.
        pause
        exit /b 1
    )
)

wsl.exe --cd "%~dp0." -e bash ./am2r-sd.sh
pause
exit /b

:nowsl
echo This tool runs in WSL (Windows Subsystem for Linux), which is not installed yet.
echo.
choice /C YN /M "Install WSL now? It needs administrator rights and a restart"
if errorlevel 2 exit /b 1
powershell -NoProfile -Command "Start-Process wsl.exe -ArgumentList '--install' -Verb RunAs -Wait"
echo.
echo When the installation has finished: restart Windows, open Ubuntu once from the Start menu to create your
echo Linux user, then run this file again.
pause
