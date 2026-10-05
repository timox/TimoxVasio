# Livrables Windows 1.1.0

| Fichier | Contenu | Usage |
| --- | --- | --- |
| `Timox VASIO Control Setup 1.1.0.exe` | Interface Electron et moteur `TimoxVirtualAsioEngine.exe` embarqué | Installation de l’interface et du moteur |
| `Timox VASIO Control 1.1.0.exe` | Version portable de l’interface et moteur embarqué | Lancement sans installation de l’interface |
| `TimoxVasio Driver 1.1.0.zip` | Pilote `TimoxVasio.dll`, script d’installation, script d’enregistrement et licence | Installation du pilote ASIO dans Windows |

Le Setup et le portable contiennent le moteur, mais pas la DLL ASIO. Le ZIP pilote contient la DLL et ses fichiers d’installation, mais pas le moteur ni l’interface. Les chemins canoniques sont listés dans [AGENTS.md](../AGENTS.md); les contrôles attendus sont détaillés dans [RELEASE_VALIDATION_1.1.0.md](RELEASE_VALIDATION_1.1.0.md).

Le pilote ASIO doit être installé avant de le sélectionner dans une application. Extraire l’archive du pilote et lancer `Installer TimoxVasio.bat` avec les droits administrateur. Ensuite installer ou lancer Timox VASIO Control. L’interface démarre le moteur et ouvre son API locale; Swagger est accessible depuis la vue API.

Le retrait du pilote s’effectue depuis une console administrateur avec `register_drivers.ps1 uninstall`. Fermer les applications audio avant de retirer le pilote.
