# Pilote TimoxVasio 1.1.0

Cette archive contient le pilote ASIO x64, son installateur et la licence. Elle ne contient ni l’interface Electron ni le moteur audio.

1. Extraire tous les fichiers dans le même dossier.
2. Exécuter `Installer TimoxVasio.bat` en administrateur.
3. Fermer puis relancer les applications audio qui utilisaient déjà TimoxVasio. Une application ouverte garde l’ancienne DLL en mémoire jusqu’à son redémarrage.
4. Installer ou lancer séparément `Timox VASIO Control`.

Le pilote 1.1.0 est installé dans `C:\Program Files\Steinberg\VirtualASIO\1.1.0\TimoxVasio.dll`. Une ancienne DLL peut rester sur disque si elle est encore ouverte; l’enregistrement ASIO pointe vers la version 1.1.0.

Pour vérifier l’enregistrement, exécuter `register_drivers.ps1 list` dans PowerShell. Pour le retirer, exécuter `register_drivers.ps1 uninstall` en administrateur.
