# TimoxVasio 1.1.0

Cette version réunit le pilote ASIO virtuel, le moteur audio et Timox VASIO Control.

## Fichiers à télécharger

| Fichier | Rôle |
| --- | --- |
| `TimoxVasio Driver 1.1.0.zip` | Pilote ASIO `TimoxVasio.dll` et installateur du pilote à lancer en administrateur. |
| `Timox VASIO Control Setup 1.1.0.exe` | Installation de l’interface et du moteur audio. |
| `Timox VASIO Control 1.1.0.exe` | Version portable de l’interface et du moteur audio. |

Installer le pilote depuis le ZIP, puis installer ou lancer Timox VASIO Control. Ouvrir l’interface avant l’application ASIO pour que le moteur et son API locale soient disponibles. La procédure détaillée est dans [INSTALL.md](../INSTALL.md).

## Changements

- Routage entre applications ASIO et pilote physique, avec profils de canaux par application.
- Routes persistantes liées au nom de l’exécutable : elles attendent les canaux d’une application absente et se réactivent lors de sa reconnexion, même avec un nouveau PID.
- API et Swagger intégrés ; l’état distingue les routes configurées des routes actives.
- Interface réorganisée avec niveaux par canal actif, analyse de corrélation stéréo L/R, console API et journaux.
- Contrôle post-installation de la DLL enregistrée, du moteur lancé et de la version de l’API.

Les builds, tests natifs et tests de l’interface passent. Les empreintes SHA-256 des artefacts figurent dans `gui/dist/release-validation-1.1.0/MANIFEST.json` lors d’un build local. Le diagnostic de la taille du tampon et des ratés audio est inscrit à la [feuille de route](ROADMAP.md) pour une version ultérieure.
