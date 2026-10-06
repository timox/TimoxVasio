# Livrables Windows 1.1.0

**Français** | [English](RELEASE_WINDOWS_1.1.0.en.md)

| Fichier | Contenu | Usage |
| --- | --- | --- |
| `Timox.VASIO.Control.Setup.1.1.0.exe` | Interface Electron et moteur `TimoxVirtualAsioEngine.exe` embarqué | Installation de l’interface et du moteur |
| `Timox.VASIO.Control.1.1.0.exe` | Version portable de l’interface et moteur embarqué | Lancement sans installation de l’interface |
| `TimoxVasio.Driver.1.1.0.zip` | Pilote `TimoxVasio.dll`, script d’installation, script d’enregistrement et licence | Installation du pilote ASIO dans Windows |
| `MANIFEST.json` | Commit, versions et empreintes SHA-256 des artefacts | Vérification des téléchargements |

Ces noms sont ceux des fichiers publiés sur GitHub; les fichiers d’un build local peuvent contenir des espaces à la place des points. Le Setup et le portable contiennent le moteur, mais pas la DLL ASIO. Le ZIP pilote contient la DLL et ses fichiers d’installation, mais pas le moteur ni l’interface. La procédure figure dans [INSTALL.md](../INSTALL.md).

Le pilote ASIO doit être installé avant de le sélectionner dans une application. Extraire l’archive du pilote et lancer `Installer TimoxVasio.bat` avec les droits administrateur. Il est copié dans un dossier `1.1.0` et enregistré depuis ce chemin; une application déjà ouverte doit être relancée pour charger cette version. Ensuite installer ou lancer Timox VASIO Control. L’interface démarre le moteur et ouvre son API locale; Swagger est accessible depuis la vue API.

Le retrait du pilote s’effectue depuis une console administrateur avec `register_drivers.ps1 uninstall`. Fermer les applications audio avant de retirer le pilote.
