# Installation de TimoxVasio

**Français** | [English](INSTALL.en.md)

Le Setup de Timox VASIO Control installe l'interface, le moteur et le pilote ASIO `TimoxVasio.dll` en une opération. La version portable n'enregistre pas le pilote. Une archive du pilote reste disponible pour une installation manuelle. La DLL et le moteur doivent être de la même version.

Prérequis : Windows 10 ou 11 x64, des applications ASIO 64 bits, les droits administrateur pour enregistrer le pilote et, pour sortir du son, un pilote ASIO physique.

## Installer depuis une release

1. Télécharger `Timox.VASIO.Control.Setup.<version>.exe` depuis la [dernière release](https://github.com/timox/TimoxVasio/releases/latest).
2. Lancer le Setup et accepter la demande de droits administrateur Windows. Il copie le pilote dans `C:\Program Files\Steinberg\VirtualASIO\<version>\` et l'enregistre sous le nom ASIO `TimoxVasio`. Le dossier est versionné pour préserver une ancienne DLL encore chargée.
3. Relancer les applications audio qui avaient chargé une ancienne DLL.

La version portable `Timox.VASIO.Control.<version>.exe` lance l'interface et le moteur sans installer le pilote. Si nécessaire, installer celui-ci depuis `TimoxVasio.Driver.<version>.zip` en exécutant `Installer TimoxVasio.bat` en administrateur après extraction.

Pour enregistrer la DLL à la main depuis le dossier extrait, dans un PowerShell administrateur :

```powershell
$driverDir = 'C:\Program Files\Steinberg\VirtualASIO\1.2.5'
New-Item -ItemType Directory -Force -Path $driverDir | Out-Null
Copy-Item -LiteralPath '.\TimoxVasio.dll' -Destination (Join-Path $driverDir 'TimoxVasio.dll') -Force
pwsh -NoProfile -File '.\register_drivers.ps1' install -DllDirectory $driverDir
```

L'enregistrement retire uniquement les anciennes entrées TimoxVasio et VASIO1–VASIO4 ; les autres pilotes ASIO installés ne sont pas touchés.

## Vérifier et démarrer

Depuis le dossier extrait (ou `install\` dans le dépôt) :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 list
Get-ChildItem "C:\Program Files\Steinberg\VirtualASIO\1.2.5\TimoxVasio.dll"
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

Le script compile la cible `TimoxVasio` dans `build_driver_110`, copie `TimoxVasio.dll` dans `C:\Program Files\Steinberg\VirtualASIO\1.2.5`, retire les anciennes entrées VASIO1–VASIO4 et enregistre le pilote. Si une ancienne DLL reste verrouillée par une application, il le signale ; elle n'est plus enregistrée comme pilote.

Le moteur et l'application se compilent séparément : voir [BUILD_DRIVERS.md](BUILD_DRIVERS.md) et [gui/README.md](../gui/README.md).

## Désinstaller

La désinstallation du Setup retire aussi l'enregistrement du pilote, uniquement s'il pointe encore vers sa DLL. Si une application audio la garde ouverte, le fichier peut rester sur disque jusqu'à sa fermeture. Pour une installation manuelle depuis l'archive, dans un PowerShell administrateur depuis le dossier extrait (ou `install\` dans le dépôt) :

```powershell
pwsh -NoProfile -File .\register_drivers.ps1 uninstall
```

La commande retire l'enregistrement ASIO et COM. Les fichiers sous `C:\Program Files\Steinberg\VirtualASIO\` restent à supprimer à la main, une fois les applications audio fermées. Timox VASIO Control se désinstalle depuis les paramètres Windows.
