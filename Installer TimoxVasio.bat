@echo off
setlocal

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Une console administrateur est requise pour installer le pilote ASIO.
    echo Relancez ce fichier avec "Executer en tant qu'administrateur".
    exit /b 1
)

set "DRIVER_DIR=%ProgramFiles%\Steinberg\VirtualASIO\1.1.1"
if not exist "%~dp0TimoxVasio.dll" (
    echo TimoxVasio.dll est introuvable a cote de ce script.
    exit /b 1
)
if not exist "%DRIVER_DIR%" mkdir "%DRIVER_DIR%"
if %errorlevel% neq 0 exit /b 1

if exist "%DRIVER_DIR%\TimoxVasio.dll" (
    fc /b "%~dp0TimoxVasio.dll" "%DRIVER_DIR%\TimoxVasio.dll" >nul
    if not errorlevel 1 goto register_driver
)
copy /y "%~dp0TimoxVasio.dll" "%DRIVER_DIR%\TimoxVasio.dll" >nul
if %errorlevel% neq 0 exit /b 1

:register_driver
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register_drivers.ps1" install -DllDirectory "%DRIVER_DIR%"
if %errorlevel% neq 0 (
    echo L'enregistrement du pilote a echoue.
    exit /b 1
)

echo Installation de TimoxVasio terminee. Relancez vos applications audio.
exit /b 0
