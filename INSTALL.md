# Installation de TimoxVasio

**Français** | [English](INSTALL.en.md)

Ce guide installe l’unique pilote ASIO virtuel `TimoxVasio.dll`. Il ne nécessite pas l’interface graphique de Visual Studio, PortAudio ni l’ouverture de `vasio.sln`.

## Prérequis

- Windows 10 ou 11 x64 ;
- Visual Studio 2026 avec les outils C++ ;
- CMake 3.16 ou supérieur ;
- le SDK ASIO présent dans `asiosdk` ;
- un terminal administrateur pour copier et enregistrer la DLL.

Les instructions de compilation sont dans `BUILD_DRIVERS.md`.

## Installation automatique

Dans un terminal administrateur :

```powershell
cd C:\Users\timo\Documents\GitHub\TimoxVasio
.\build_and_install.bat
```

Le script configure et compile la cible `TimoxVasio` dans `build_driver_110`, copie `TimoxVasio.dll` dans `C:\Program Files\Steinberg\VirtualASIO\1.1.0`, puis enregistre le pilote. Il retire les anciennes entrées VASIO1–VASIO4 connues et tente de supprimer leurs DLL. Si une ancienne DLL reste verrouillée, le script le signale; elle n’est plus enregistrée comme pilote.

## Installer la DLL téléchargée depuis une release

Le Setup Electron installe l’interface et son moteur; il n’installe pas le pilote ASIO. Télécharger `TimoxVasio.Driver.1.1.0.zip` depuis la release, l’extraire, puis lancer `Installer TimoxVasio.bat` en administrateur depuis le dossier extrait. Le script installe la DLL dans un dossier versionné afin de ne pas écraser un ancien pilote encore chargé par une application.

Pour effectuer la même opération manuellement depuis le dossier extrait, ouvrir PowerShell en administrateur :

```powershell
$driverDir = 'C:\Program Files\Steinberg\VirtualASIO\1.1.0'
New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
Copy-Item -LiteralPath '.\TimoxVasio.dll' -Destination (Join-Path $driverDir 'TimoxVasio.dll') -Force
pwsh -NoProfile -File '.\register_drivers.ps1' install -DllDirectory $driverDir
```

Cette opération inscrit le pilote sous le nom ASIO `TimoxVasio`. Elle retire uniquement les entrées connues de TimoxVasio et VASIO historiques. Les autres pilotes ASIO installés restent inchangés.

## Vérification

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
Get-ChildItem "C:\Program Files\Steinberg\VirtualASIO\1.1.0\TimoxVasio.dll"
pwsh -NoProfile -File .\tests\verify-installed-stack.ps1
```

Fermer puis relancer les applications audio après l’enregistrement. Le pilote ASIO attendu est `TimoxVasio` et il annonce 256 entrées et 256 sorties. Chaque application n’expose toutefois que les canaux qu’elle a réellement alloués avec `createBuffers`.

Pour router l’audio, lancer `TimoxVirtualAsioEngine.exe` ou Timox VASIO Control, choisir le pilote ASIO physique, appliquer la fréquence et la taille de buffer acceptées par ce pilote, puis construire les routes dans l’interface. Le circuit matériel complet doit être validé avec le matériel utilisé.

Avant d’ouvrir Renoise ou une autre application ASIO, vérifier que Timox VASIO Control est connecté et que `http://127.0.0.1:52525/api/v1/state` répond. Le moteur doit être lancé même si l’interface est ensuite fermée. Une route liée à une application absente reste enregistrée et sera activée après l’ouverture de ses canaux ; la vue d’état distingue les routes `configuredRoutes` des routes `routes` effectivement actives.

## Désinstallation

Dans un terminal administrateur placé dans le dépôt :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```
