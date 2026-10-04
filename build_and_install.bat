@echo off
setlocal enabledelayedexpansion

:: Script de compilation et installation du pilote TimoxVasio
:: Exécuter avec privilèges administrateur

echo.
echo ========================================
echo  Compilation de TimoxVasio (x64)
echo ========================================
echo.

:: Vérifier les droits administrateur
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERREUR] Ce script necessite les droits administrateur
    echo Relancez en tant qu'administrateur
    pause
    exit /b 1
)

:: Vérifier la présence de CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERREUR] CMake non trouvé
    echo Installez CMake depuis https://cmake.org/download/
    pause
    exit /b 1
)

:: Vérifier la présence du ASIO SDK
if not exist "asiosdk\common\asio.h" (
    echo [ERREUR] ASIO SDK non trouvé
    echo Téléchargez depuis https://www.steinberg.net/asiosdk
    echo et extrayez dans le dossier "asiosdk"
    pause
    exit /b 1
)

:: Charger les outils du Visual Studio installé afin d'utiliser le compilateur x64 2026
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERREUR] vswhere.exe introuvable ; installez Visual Studio 2026 avec les outils C++
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo [ERREUR] Installation de Visual Studio introuvable
    exit /b 1
)
if not exist "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" (
    echo [ERREUR] VsDevCmd.bat introuvable dans "%VSINSTALL%"
    exit /b 1
)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64
if %errorlevel% neq 0 exit /b 1

echo [OK] Prérequis vérifiés
echo.

:: Configuration CMake
echo [INFO] Configuration CMake...
cmake -G Ninja ^
    -S . -B build_drivers_vs2026_ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DASIO_SDK_PATH="%CD%\asiosdk" ^
    -DVASIO_BUILD_DRIVERS=ON

if %errorlevel% neq 0 (
    echo [ERREUR] Configuration CMake échouée
    pause
    exit /b 1
)

:: Compilation
echo.
echo [INFO] Compilation du pilote TimoxVasio...
cmake --build build_drivers_vs2026_ninja --target TimoxVasio

if %errorlevel% neq 0 (
    echo [ERREUR] Compilation échouée
    pause
    exit /b 1
)

echo [OK] Compilation réussie
echo.

:: Vérifier les DLLs
for %%D in (TimoxVasio.dll) do (
    if not exist "build_drivers_vs2026_ninja\%%D" (
        echo [ERREUR] DLL manquante : build_drivers_vs2026_ninja\%%D
        exit /b 1
    )
)

echo [OK] DLL générée:
echo   - build_drivers_vs2026_ninja\TimoxVasio.dll
echo.

:: Créer le dossier d'installation
echo [INFO] Création du dossier d'installation...
if not exist "C:\Program Files\Steinberg\VirtualASIO" (
    mkdir "C:\Program Files\Steinberg\VirtualASIO"
)

:: Copier les DLLs
echo [INFO] Copie de TimoxVasio.dll...
copy build_drivers_vs2026_ninja\TimoxVasio.dll "C:\Program Files\Steinberg\VirtualASIO\" /Y

if %errorlevel% neq 0 (
    echo [ERREUR] Copie des DLLs échouée
    pause
    exit /b 1
)

echo [OK] DLLs installées dans C:\Program Files\Steinberg\VirtualASIO\
for %%D in (VASIO1.dll VASIO2.dll VASIO3.dll VASIO4.dll) do (
    if exist "C:\Program Files\Steinberg\VirtualASIO\%%D" del /f /q "C:\Program Files\Steinberg\VirtualASIO\%%D" 2>nul
    if exist "C:\Program Files\Steinberg\VirtualASIO\%%D" echo [AVERTISSEMENT] Ancienne DLL encore presente: %%D
)
echo.

:: Enregistrement des drivers
echo [INFO] Enregistrement de TimoxVasio...
powershell -ExecutionPolicy Bypass -File register_drivers.ps1 install

if %errorlevel% neq 0 (
    echo [ERREUR] Enregistrement échoué
    pause
    exit /b 1
)

echo.
echo ========================================
echo  Installation réussie!
echo ========================================
echo.
echo Le pilote TimoxVasio est maintenant disponible
echo dans votre DAW (Reaper, MIXXX, etc)
echo.
echo Prochaines étapes:
echo   1. Relancez votre DAW
echo   2. Allez dans Audio Device Settings
echo   3. Sélectionnez TimoxVasio comme pilote audio
echo.
pause
