# Démarrage rapide de TimoxVasio

**Français** | [English](QUICKSTART.en.md)

Le projet expose un pilote ASIO virtuel unique, `TimoxVasio.dll`, et un moteur audio distinct, `TimoxVirtualAsioEngine.exe`. Le pilote annonce jusqu’à 256 entrées et 256 sorties; l’API ne publie que les canaux effectivement alloués par chaque client.

## Compiler

Depuis un Developer Command Prompt Visual Studio 2026 x64, à la racine du dépôt :

```powershell
cmake -S . -B build_drivers_vs2026_ninja -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_drivers_vs2026_ninja --target TimoxVasio DriverProbe DriverAudioProbe

cmake -S . -B build_engine_vs2026_ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_engine_vs2026_ninja --target TimoxVirtualAsioEngine
```

La compilation produit `build_drivers_vs2026_ninja/TimoxVasio.dll` et `build_engine_vs2026_ninja/TimoxVirtualAsioEngine.exe`.

## Installer et vérifier le pilote

Dans un terminal administrateur :

```powershell
.\build_and_install.bat
pwsh -NoProfile -File .\register_drivers.ps1 list
```

Le nom enregistré attendu est `TimoxVasio`. Pour vérifier COM sans démarrer de flux audio :

```powershell
.\build_drivers_vs2026_ninja\DriverProbe.exe `
  --dll .\build_drivers_vs2026_ninja\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

Voir [INSTALL.md](INSTALL.md) pour l’installation et [BUILD_DRIVERS.md](BUILD_DRIVERS.md) pour les probes.

## Démarrer et router

Lancer `TimoxVirtualAsioEngine.exe` seul pour démarrer le service local, ou lancer Timox VASIO Control qui démarre ce moteur. Dans l’interface, choisir un pilote ASIO physique, appliquer ses valeurs de fréquence et de buffer, puis créer les routes à partir des endpoints proposés par l’API.

La validation complète doit être effectuée avec un hôte ASIO réel et un signal observé sur le matériel. Voir [TEST_VERIFICATION.md](TEST_VERIFICATION.md).
