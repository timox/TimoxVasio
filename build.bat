@echo off
REM Script de compilation du moteur TimoxVirtualAsioEngine sous Windows

echo.
echo === TimoxVirtualAsioEngine - Build Script ===
echo.

REM Verifier Visual Studio
if not defined VSINSTALLDIR (
    echo Erreur: Visual Studio n'est pas dans le PATH
    echo Veuillez ouvrir le Developer Command Prompt for Visual Studio
    echo ou executer vcvarsall.bat avant ce script
    exit /b 1
)

REM Verifier ASIO SDK
if not exist "asiosdk\common\asio.h" (
    echo.
    echo Erreur: ASIO SDK non trouve
    echo.
    echo Instructions:
    echo 1. Telecharger ASIO SDK depuis: https://www.steinberg.net/asiosdk
    echo 2. Extraire le contenu dans le dossier "asiosdk"
    echo 3. Relancer ce script
    echo.
    exit /b 1
)

echo ASIO SDK detecte: asiosdk\common\asio.h
echo.

REM Configurer et compiler le moteur avec Visual Studio
echo Compilation du moteur TimoxVirtualAsioEngine...
cmake -S . -B build_engine_vs2026 -G "Visual Studio 18 2026" -A x64
if %ERRORLEVEL% neq 0 (
    echo Erreur lors de la generation CMake
    exit /b 1
)

cmake --build build_engine_vs2026 --config Release --target TimoxVirtualAsioEngine
if %ERRORLEVEL% neq 0 (
    echo Erreur lors de la compilation
    exit /b 1
)

echo.
echo === Compilation reussie ===
echo.
echo L'executable se trouve dans: build_engine_vs2026\Release\TimoxVirtualAsioEngine.exe
echo.

REM Alternative: compilation directe avec msbuild
REM echo Compilation avec MSBuild...
REM msbuild ASIOHost.vcxproj /p:Configuration=Release /p:Platform=x64
