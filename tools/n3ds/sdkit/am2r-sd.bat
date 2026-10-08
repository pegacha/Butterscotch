@echo off
rem AM2R 3DS - SD card builder for Windows: runs am2r-sd.sh in WSL (Windows Subsystem for Linux), offering to
rem install WSL first if there is none. am2r-sd.sh is downloaded next to this file if it is missing.
rem Uses the default distro, or else the first one that starts (Docker Desktop's are skipped). To pick one:
rem   set AM2R_WSL_DISTRO=<name>   before running this (names: wsl -l -v).
setlocal
title AM2R 3DS - SD card builder
set "REPO=pegacha/Butterscotch"
rem Where am2r-sd.sh comes from if it is missing (a Codeberg release's copy has its own).
set "SHURL=https://github.com/%REPO%/releases/latest/download/am2r-sd.sh"
cd /d "%~dp0"

rem A 32-bit cmd sees SysWOW64, which has no wsl.exe; Sysnative is the real System32 there.
set "WSL=wsl.exe"
if exist "%SystemRoot%\Sysnative\wsl.exe" (
    set "WSL=%SystemRoot%\Sysnative\wsl.exe"
) else (
    where wsl.exe >nul 2>nul || goto :nowsl
)

set "DISTRO=%AM2R_WSL_DISTRO%"
if defined DISTRO goto :havedistro
rem The default distro, if it starts.
"%WSL%" -e true >nul 2>nul && goto :havedistro
rem Otherwise the first one that starts. wsl -l -q prints UTF-16, which cmd can't read: PowerShell reads it and
rem prints the names back in the console's own encoding.
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "$env:WSL_UTF8=$null; $e=[Console]::OutputEncoding; [Console]::OutputEncoding=[Text.Encoding]::Unicode; $n=& '%WSL%' -l -q; [Console]::OutputEncoding=$e; $n | ForEach-Object { ($_ -replace [char]0,'').Trim() } | Where-Object { $_ -and $_ -notlike 'docker-desktop*' }"`) do (
    if not defined DISTRO (
        "%WSL%" -d "%%D" -e true >nul 2>nul && set "DISTRO=%%D"
    )
)
if not defined DISTRO goto :nowsl

:havedistro
set "DARG="
if defined DISTRO (
    echo Using WSL distro: %DISTRO%
    set DARG=-d "%DISTRO%"
)

if not exist "am2r-sd.sh" (
    echo Downloading am2r-sd.sh ...
    powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing -Uri '%SHURL%' -OutFile 'am2r-sd.sh'"
    if not exist "am2r-sd.sh" (
        echo Could not download am2r-sd.sh. Check the internet connection and try again.
        pause
        exit /b 1
    )
)

"%WSL%" %DARG% --cd "%~dp0." -e bash ./am2r-sd.sh
pause
exit /b

:nowsl
echo No usable WSL (Windows Subsystem for Linux) distro was found.
"%WSL%" --status >nul 2>nul && (
    echo What WSL reports:
    "%WSL%" -l -v
    "%WSL%" -e true
    echo.
    echo If a distro is listed above, run this again with:   set AM2R_WSL_DISTRO=^<its name^>
)
echo.
choice /C YN /M "Install WSL with Ubuntu now? It needs administrator rights and a restart"
if errorlevel 2 exit /b 1
powershell -NoProfile -Command "Start-Process '%WSL%' -ArgumentList '--install' -Verb RunAs -Wait"
echo.
echo When the installation has finished: restart Windows, open Ubuntu once from the Start menu to create your
echo Linux user, then run this file again.
pause
