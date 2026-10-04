# Vérification de TimoxVasio et du moteur

Les sondes logicielles vérifient des contrats distincts. Elles ne prouvent pas à elles seules qu’un hôte ASIO réel a alloué des canaux ni qu’un signal a traversé le matériel.

## Build x64

Dans un Developer Command Prompt Visual Studio 2026 x64 :

```powershell
cmake --build build_drivers_vs2026_ninja --target TimoxVasio DriverProbe DriverAudioProbe
cmake --build build_engine_vs2026_ninja --target TimoxVirtualAsioEngine AudioTransportProbe AudioRoutingRuntimeTests RoutingGraphTests
```

## Pilote COM et transport

Vérifier l’identité COM directe et la capacité 256/256 :

```powershell
.\build_drivers_vs2026_ninja\DriverProbe.exe `
  --dll .\build_drivers_vs2026_ninja\TimoxVasio.dll `
  --clsid "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
```

Les sondes du moteur couvrent le transport interprocessus et le graphe, y compris les canaux virtuels élevés et les index physiques clairsemés :

```powershell
.\build_engine_vs2026_ninja\AudioTransportProbe.exe
.\build_engine_vs2026_ninja\AudioRoutingRuntimeTests.exe
.\build_engine_vs2026_ninja\RoutingGraphTests.exe
```

## Registre ASIO

La sonde enregistrée appelle l’énumérateur Steinberg et vérifie l’identité et les canaux du pilote. Elle ne démarre pas de flux audio :

```powershell
.\build_drivers_vs2026_ninja\DriverProbe.exe --registered TimoxVasio
```

`tests/driver-registration.Tests.ps1` vérifie la migration idempotente dans une racine HKCU temporaire. `tests/registered-driver-smoke.ps1` cible HKLM, installe provisoirement TimoxVasio et retire son enregistrement à la fin.

## Contrat API et interface

```powershell
pwsh -NoProfile -File .\tests\api-contract.ps1
cd gui
npm run contract-test
$env:CI='true'
npm run react-test -- --watchAll=false --runInBand
node --check electron/main.js
```

## Hôte ASIO réel et circuit matériel

Le moteur doit démarrer avant l’hôte : `TimoxVasio::init()` attend l’attachement du moteur. Dans Mixxx, ouvrir **Preferences > Sound Hardware**, choisir **ASIO** comme *Sound API*, puis sélectionner `TimoxVasio` dans le périphérique de sortie. L’API ASIO est distincte du nom du périphérique. Après sélection/allocation, vérifier que l’API du moteur publie le processus et ses endpoints actifs.

Pour le probe ASIO externe, démarrer le nouveau `TimoxVirtualAsioEngine.exe`, puis lancer `VasioExternalHostProbe.exe --driver TimoxVasio`. Le probe initialise le client et teste `createBuffers`, `start` et les cycles d’arrêt/réouverture. Ne pas le lancer contre un moteur déjà actif d’identité ou de session inconnue.

L’acceptation audio complète exige ensuite de sélectionner un pilote physique dans VASIO Control, d’appliquer le taux et le buffer confirmés, de créer les routes dans l’interface, puis d’observer un signal sur les canaux physiques utilisés. Vérifier les entrées ADAT à plusieurs fréquences si le matériel sélectionné les expose. L’énumération, l’activation COM et les probes synthétiques ne remplacent pas cette vérification.
