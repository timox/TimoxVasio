# Démarrage rapide de TimoxVasio

**Français** | [English](QUICKSTART.en.md)

Le projet expose un pilote ASIO virtuel unique, `TimoxVasio.dll`, et un moteur audio distinct, `TimoxVirtualAsioEngine.exe`. Le pilote annonce jusqu’à 256 entrées et 256 sorties; l’API ne publie que les canaux effectivement alloués par chaque client.

## Compiler

Depuis un Developer Command Prompt Visual Studio 2026 x64, à la racine du dépôt :

```powershell
cmake -S . -B build_driver_110 -G "Visual Studio 18 2026" -A x64 `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_driver_110 --config Release --target TimoxVasio DriverProbe DriverAudioProbe

cmake -S . -B build_codex_110 -G "Visual Studio 18 2026" -A x64
cmake --build build_codex_110 --config Release --target TimoxVirtualAsioEngine
```

La compilation produit `build_driver_110/Release/TimoxVasio.dll` et `build_codex_110/Release/TimoxVirtualAsioEngine.exe`.

## Installer et vérifier le pilote

Dans un terminal administrateur :

```powershell
.\install\build_and_install.bat
pwsh -NoProfile -File .\install\register_drivers.ps1 list
```

Le nom enregistré attendu est `TimoxVasio`. Pour vérifier COM sans démarrer de flux audio :

```powershell
.\build_driver_110\Release\DriverProbe.exe `
  --dll .\build_driver_110\Release\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

Voir [INSTALL.md](INSTALL.md) pour l’installation et [BUILD_DRIVERS.md](BUILD_DRIVERS.md) pour les probes.

## Démarrer et router

Lancer `TimoxVirtualAsioEngine.exe` seul pour démarrer le service local, ou lancer Timox VASIO Control qui démarre ce moteur. Dans l’interface, choisir un pilote ASIO physique, appliquer ses valeurs de fréquence et de buffer, puis créer les routes à partir des endpoints proposés par l’API.

La validation complète doit être effectuée avec un hôte ASIO réel et un signal observé sur le matériel. Voir [TEST_VERIFICATION.md](TEST_VERIFICATION.md).
