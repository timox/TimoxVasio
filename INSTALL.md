# Installation de TimoxVasio

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
cd C:\Users\timo\Documents\GitHub\grrzzzz\asio
.\build_and_install.bat
```

Le script configure et compile la cible `TimoxVasio`, copie `TimoxVasio.dll` dans `C:\Program Files\Steinberg\VirtualASIO`, puis enregistre le pilote. Il retire les anciennes entrées VASIO1–VASIO4 connues et tente de supprimer leurs DLL. Si une ancienne DLL reste verrouillée, le script le signale; elle n’est plus enregistrée comme pilote.

## Vérification

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
Get-ChildItem "C:\Program Files\Steinberg\VirtualASIO\TimoxVasio.dll"
```

Fermer puis relancer les applications audio après l’enregistrement. Le pilote ASIO attendu est `TimoxVasio` et il annonce 256 entrées et 256 sorties. Chaque application n’expose toutefois que les canaux qu’elle a réellement alloués avec `createBuffers`.

Pour router l’audio, lancer `TimoxVirtualAsioEngine.exe` ou VASIO Control, choisir le pilote ASIO physique, appliquer la fréquence et la taille de buffer acceptées par ce pilote, puis construire les routes dans l’interface. Le circuit matériel complet doit être validé avec le matériel utilisé.

## Désinstallation

Dans un terminal administrateur placé dans le dépôt :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```
