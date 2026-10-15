# Livrables Windows 1.2.0

Le ZIP pilote contient la DLL ASIO et son installateur. Le Setup et l'exécutable portable contiennent Control et le moteur audio ; ils n'installent pas la DLL ASIO.

Télécharger les quatre fichiers de la [release 1.2.0](https://github.com/timox/TimoxVasio/releases/tag/v1.2.0) : `TimoxVasio.Driver.1.2.0.zip`, `Timox.VASIO.Control.Setup.1.2.0.exe`, `Timox.VASIO.Control.1.2.0.exe` et `MANIFEST.json`.

Extraire le ZIP, puis exécuter `Installer TimoxVasio.bat` en administrateur. Il enregistre la DLL depuis `C:\Program Files\Steinberg\VirtualASIO\1.2.0`. Relancer les applications audio qui utilisaient déjà l'ancienne DLL. Installer ou lancer ensuite Control.

Le manifeste donne le commit source et les empreintes SHA-256 de chaque livrable. Voir les [notes de version](RELEASE_NOTES_v1.2.0.md) et le [guide d'installation](INSTALL.md).
