@echo off
rem Installs the Harmonizer VST3. Double-click to run.
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo Harmonizer plugin installer
echo ===========================
echo.

if not exist "Harmonizer.vst3" (
    echo Could not find Harmonizer.vst3 next to this script.
    echo Extract the whole zip first, then run this from inside the extracted folder.
    echo.
    pause
    exit /b 1
)

set "SHARED=%CommonProgramFiles%\VST3"
set "USERDIR=%LOCALAPPDATA%\Programs\Common\VST3"

rem Try the machine-wide folder every DAW scans by default. It needs
rem administrator rights, so fall back to the per-user folder rather than
rem failing outright.
set "TARGET=%SHARED%"
mkdir "%SHARED%" 2>nul
copy /y nul "%SHARED%\.harmonizer-write-test" >nul 2>&1
if errorlevel 1 (
    set "TARGET=%USERDIR%"
    echo No administrator rights, installing just for you instead.
) else (
    del "%SHARED%\.harmonizer-write-test" >nul 2>&1
)

echo Installing to "!TARGET!"
if exist "!TARGET!\Harmonizer.vst3" rmdir /s /q "!TARGET!\Harmonizer.vst3"
mkdir "!TARGET!" 2>nul
xcopy /e /i /y /q "Harmonizer.vst3" "!TARGET!\Harmonizer.vst3" >nul
if errorlevel 1 (
    echo.
    echo Copy failed. Close your DAW and try again.
    pause
    exit /b 1
)

echo.
echo Done.
echo   VST3 -^> !TARGET!\Harmonizer.vst3
echo.
if "!TARGET!"=="%USERDIR%" (
    echo Because this went to your personal folder, add it to your DAW's plugin
    echo search paths if it does not show up:
    echo   !TARGET!
    echo In FL Studio: Options ^> Manage plugins ^> add the folder, then Find plugins.
    echo.
)
echo Next: close your DAW completely and reopen it, then rescan plugins.
echo.
pause
