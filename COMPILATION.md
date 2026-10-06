# Compilation de TimoxVasio

**Français** | [English](COMPILATION.en.md)

## Prérequis

- Windows 10 ou 11 x64 ;
- Visual Studio 2026 avec la charge de travail C++ ;
- CMake et Ninja ;
- le SDK ASIO dans `asiosdk`.

L’interface graphique de Visual Studio et `vasio.sln` ne sont pas nécessaires. Utiliser un Developer Command Prompt for VS 2026 configuré en x64.

## Configurer et compiler

Depuis la racine `asio` :

```powershell
cmake -S . -B build_drivers_vs2026_ninja `
  -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_drivers_vs2026_ninja --target TimoxVasio DriverProbe DriverAudioProbe
```

Le fichier produit est `build_drivers_vs2026_ninja\TimoxVasio.dll`. `VASIO_BUILD_DRIVERS` est le nom historique de l’option CMake ; il active la cible unique `TimoxVasio`.

La sonde directe est documentée dans [BUILD_DRIVERS.md](BUILD_DRIVERS.md). Pour installer la DLL dans `Program Files` et l’enregistrer, exécuter `build_and_install.bat` dans un terminal administrateur.

## Compiler le moteur

La configuration normale produit l’exécutable du moteur :

```powershell
cmake -S . -B build_engine_vs2026_ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_engine_vs2026_ninja --target TimoxVirtualAsioEngine
```

La cible CMake et le fichier produit sont nommés `TimoxVirtualAsioEngine`. La compilation du moteur seule ne produit pas `TimoxVasio.dll`.
