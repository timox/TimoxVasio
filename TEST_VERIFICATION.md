# Vérification de TimoxVasio et du moteur

**Français** | [English](TEST_VERIFICATION.en.md)

Le mainteneur a confirmé que les essais de bout en bout sur hôte ASIO et matériel ont été réalisés dans son environnement. Ce document donne les commandes de vérification reproductible du code et des contrats API; il ne présente pas ces essais comme restant à faire.

## Build x64

Dans un Developer Command Prompt Visual Studio 2026 x64 :

```powershell
cmake --build build_driver_110 --config Release --target TimoxVasio DriverProbe DriverAudioProbe DriverCompatibilityProfileTests
cmake --build build_codex_110 --config Release --target TimoxVirtualAsioEngine AudioTransportProbe AudioRoutingRuntimeTests RoutingGraphTests ApplicationProfilesApiTests
```

## Pilote COM et transport

Vérifier l’identité COM directe et la capacité 256/256 :

```powershell
.\build_driver_110\Release\DriverProbe.exe `
  --dll .\build_driver_110\Release\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

Les sondes du moteur couvrent le transport interprocessus et le graphe, y compris les canaux virtuels élevés et les index physiques clairsemés :

```powershell
.\build_codex_110\Release\AudioTransportProbe.exe
.\build_codex_110\Release\AudioRoutingRuntimeTests.exe
.\build_codex_110\Release\RoutingGraphTests.exe
```

## Registre ASIO

La sonde enregistrée appelle l’énumérateur Steinberg et vérifie l’identité et les canaux du pilote. Elle ne démarre pas de flux audio :

```powershell
.\build_driver_110\Release\DriverProbe.exe --registered TimoxVasio
```

`tests/driver-registration.Tests.ps1` vérifie la migration idempotente dans une racine HKCU temporaire. `tests/registered-driver-smoke.ps1` lit l’installation HKLM existante et l’active sans modifier son enregistrement.

## Contrat API et interface

```powershell
pwsh -NoProfile -File .\tests\api-contract.ps1
cd gui
npm run contract-test
$env:CI='true'
npm run react-test -- --watchAll=false --runInBand
node --check electron/main.js
```

## Parcours hôte ASIO et matériel

Le moteur doit démarrer avant l’hôte : `TimoxVasio::init()` attend l’attachement du moteur. Dans Mixxx, ouvrir **Preferences > Sound Hardware**, choisir **ASIO** comme *Sound API*, puis sélectionner `TimoxVasio` dans le périphérique de sortie. L’API ASIO est distincte du nom du périphérique. Après sélection/allocation, l’API du moteur publie le processus et ses endpoints actifs. Le mainteneur confirme avoir exécuté le parcours jusqu’au matériel dans son environnement.

Pour le probe ASIO externe, démarrer le nouveau `TimoxVirtualAsioEngine.exe`, puis lancer `VasioExternalHostProbe.exe --driver TimoxVasio`. Le probe initialise le client et teste `createBuffers`, `start` et les cycles d’arrêt/réouverture. Ne pas le lancer contre un moteur déjà actif d’identité ou de session inconnue.

Pour reproduire ce parcours, sélectionner un pilote physique dans Timox VASIO Control, appliquer le taux et le buffer confirmés, créer les routes, puis observer les canaux physiques utilisés. Les probes synthétiques restent utiles pour isoler les contrats logiciels de ces essais matériels.
