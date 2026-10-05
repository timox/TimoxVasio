# Compilation et vérification de TimoxVasio

## Environnement vérifié

- Visual Studio 2026 et Ninja depuis son Developer Command Prompt ;
- MSVC x64 19.51.36260.0 ;
- Windows SDK 10.0.26100.0 ;
- CMake 4.4.3 ;
- SDK ASIO présent dans `asiosdk`.

Lancer les commandes depuis le Developer Command Prompt for VS 2026 configuré en x64.

## Compiler la DLL

Depuis la racine `asio` :

```powershell
cmake -S . -B build_drivers_vs2026_ninja `
  -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DASIO_SDK_PATH="$PWD/asiosdk" `
  -DVASIO_BUILD_DRIVERS=ON
cmake --build build_drivers_vs2026_ninja --target TimoxVasio DriverProbe DriverAudioProbe
```

La DLL générée est `build_drivers_vs2026_ninja/TimoxVasio.dll`. La cible CMake `VASIO_BUILD_DRIVERS` reste le commutateur de compilation historique ; le pilote produit et enregistré s’appelle `TimoxVasio`.

## Vérification COM

La sonde appelle explicitement `ASIOInit`, vérifie l’activation COM, les 256 entrées/sorties et que `canSampleRate` ne publie que la fréquence de l’horloge physique attachée :

```powershell
.\build_drivers_vs2026_ninja\DriverProbe.exe `
  --dll .\build_drivers_vs2026_ninja\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

La sonde audio vérifie également les index de canaux bas, élevés et clairsemés. Le résultat du test audio de bout en bout réalisé sur ce poste est consigné dans la section « État de validation » ci-dessous.

## Vérifier l’enregistrement Steinberg

`AsioDriverList` lit les pilotes ASIO enregistrés et active les classes COM. Le test de migration dans `tests/driver-registration.Tests.ps1` utilise une clé HKCU temporaire et vérifie que les entrées ASIO sans rapport sont préservées. `tests/registered-driver-smoke.ps1` cible l’enregistrement système HKLM et requiert des privilèges administrateur ; il ne doit être exécuté que comme vérification explicite d’installation.

Pour installer sur cette machine, voir `INSTALL.md`. Pour lister ou désinstaller l’entrée du produit :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```

## État de validation

La compilation, l’activation COM directe, les sondes de transport/routage et les contrats API/UI ont été vérifiés. Après réinstallation de la version 1.0.0, le test audio de bout en bout a été confirmé par l’utilisateur le 5 octobre 2026 : Renoise, huit routes, SSL ASIO Driver 1, 48 kHz/1024 frames; l’API a publié 60 événements `audio.meter` en 3,5 secondes avec des crêtes de `-101,65` à `-26,43 dBFS`. Les diagnostics et le fichier `engine.log` ont confirmé le démarrage et la configuration.
