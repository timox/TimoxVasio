# TimoxVasio 1.2.5

Cette version rend les routes enregistrées plus lisibles et réunit l'installation de Control et du pilote ASIO dans un seul Setup.

## Changements

- Les connexions enregistrées sont regroupées par couple source/destination. Chaque groupe déplié affiche une petite matrice des canaux concernés, leur activité et les commandes de chaque connexion.
- Les routes en attente d'une application déconnectée restent regroupées sous son nom.
- Le Setup installe et enregistre `TimoxVasio.dll` avec l'interface et le moteur. Windows demande les droits administrateur. La version portable ne modifie pas l'enregistrement ASIO.
- La désinstallation retire l'enregistrement du pilote si celui-ci pointe encore vers la DLL installée par cette version.

Le correctif « Apply configuration » de la 1.2.4 est inclus. Les routes existantes sont conservées. Le moteur et le pilote portent tous deux la version 1.2.5.

## Installation

Télécharger [Timox.VASIO.Control.Setup.1.2.5.exe](https://github.com/timox/TimoxVasio/releases/tag/v1.2.5) et le lancer. Relancer ensuite les applications audio qui utilisaient l'ancienne DLL. Voir le [guide Windows](RELEASE_WINDOWS_1.2.5.md) et le [guide d'installation](INSTALL.md).
