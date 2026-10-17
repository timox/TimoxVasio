# Installation de TimoxVasio

**Français** | [English](INSTALL.en.md)

TimoxVasio se compose de deux livraisons séparées : le pilote ASIO `TimoxVasio.dll` (archive `TimoxVasio.Driver.<version>.zip`) et l'application Timox VASIO Control, qui embarque le moteur `TimoxVirtualAsioEngine.exe` (`Timox.VASIO.Control.Setup.<version>.exe` ou version portable). Le Setup n'installe pas le pilote ; l'archive du pilote n'installe pas l'application. La DLL et le moteur doivent être de la même version : le transport partagé de la 1.2.3 refuse une DLL plus ancienne.

Prérequis : Windows 10 ou 11 x64, des applications ASIO 64 bits, les droits administrateur pour enregistrer le pilote et, pour sortir du son, un pilote ASIO physique.

## Installer depuis une release

1. Télécharger les fichiers de la [dernière release](https://github.com/timox/TimoxVasio/releases/latest).
2. Extraire `TimoxVasio.Driver.<version>.zip`, puis lancer `Installer TimoxVasio.bat` **en administrateur** depuis le dossier extrait. Le script copie la DLL dans `C:\Program Files\Steinberg\VirtualASIO\<version>\` et l'enregistre sous le nom ASIO `TimoxVasio`. Le dossier est versionné : un ancien pilote encore chargé par une application n'est pas écrasé.
3. Installer `Timox.VASIO.Control.Setup.<version>.exe`, ou lancer directement `Timox.VASIO.Control.<version>.exe` (portable).
4. Relancer les applications audio qui avaient chargé une ancienne DLL.

Pour enregistrer la DLL à la main depuis le dossier extrait, dans un PowerShell administrateur :

```powershell
$driverDir = 'C:\Program Files\Steinberg\VirtualASIO\1.2.3'
New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
Copy-Item -LiteralPath '.\TimoxVasio.dll' -Destination (Join-Path $driverDir 'TimoxVasio.dll') -Force
pwsh -NoProfile -File '.\register_drivers.ps1' install -DllDirectory $driverDir
```

L'enregistrement retire uniquement les anciennes entrées TimoxVasio et VASIO1–VASIO4 ; les autres pilotes ASIO installés ne sont pas touchés.

## Vérifier et démarrer

Depuis le dossier extrait (ou `install\` dans le dépôt) :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
Get-ChildItem "C:\Program Files\Steinberg\VirtualASIO\1.2.3\TimoxVasio.dll"
```

Le pilote attendu est `TimoxVasio`. Il annonce 256 entrées et 256 sorties ; chaque application n'expose que les canaux qu'elle alloue réellement avec `createBuffers`.

Ordre de lancement : démarrer Timox VASIO Control **avant** d'ouvrir TimoxVasio dans une application audio. Dans **Configuration**, choisir le pilote ASIO physique, puis la fréquence et la taille de buffer qu'il accepte. Le moteur écoute sur `127.0.0.1`, port `52525` s'il est libre, sinon un port choisi au démarrage et annoncé par l'événement `api.ready` ; Control l'affiche. Le moteur doit rester lancé même si la fenêtre de Control est fermée. Une route enregistrée pour une application absente reste dans `configuredRoutes` et passe dans `routes` quand ses canaux s'ouvrent.

Dans un clone du dépôt, `pwsh -NoProfile -File .\tests\verify-installed-stack.ps1` contrôle l'enregistrement du pilote et le moteur installé.

## Compiler et installer depuis les sources

Prérequis supplémentaires : Visual Studio 2026 avec les outils C++, CMake, et le SDK ASIO dans `asiosdk`. Les commandes détaillées sont dans [BUILD_DRIVERS.md](BUILD_DRIVERS.md).

Dans un terminal administrateur, à la racine du dépôt :

```powershell
.\install\build_and_install.bat
```

Le script compile la cible `TimoxVasio` dans `build_driver_110`, copie `TimoxVasio.dll` dans `C:\Program Files\Steinberg\VirtualASIO\1.2.3`, retire les anciennes entrées VASIO1–VASIO4 et enregistre le pilote. Si une ancienne DLL reste verrouillée par une application, il le signale ; elle n'est plus enregistrée comme pilote.

Le moteur et l'application se compilent séparément : voir [BUILD_DRIVERS.md](BUILD_DRIVERS.md) et [gui/README.md](../gui/README.md).

## Désinstaller

Dans un PowerShell administrateur, depuis le dossier extrait de l'archive (ou `install\` dans le dépôt) :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```

La commande retire l'enregistrement ASIO et COM. Les fichiers sous `C:\Program Files\Steinberg\VirtualASIO\` restent à supprimer à la main, une fois les applications audio fermées. Timox VASIO Control se désinstalle depuis les paramètres Windows.
