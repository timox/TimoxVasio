# TimoxVasio 1.2.4

Cette version corrige l'application des routes depuis Timox VASIO Control.

## Corrections

- Un événement `routes.changed` reçu pendant une modification n'efface plus les routes en brouillon avant le clic sur « Apply configuration ».
- Les groupes de connexions enregistrées affichent séparément l'état appliqué et le nombre de routes réellement actives. Une route appliquée reste en attente tant que ses applications n'ont pas ouvert les canaux ASIO concernés.

Le moteur et le pilote sont livrés ensemble en version 1.2.4. Les routes existantes sont conservées.

## Installation

Télécharger les quatre fichiers de la [release 1.2.4](https://github.com/timox/TimoxVasio/releases/tag/v1.2.4). Installer le pilote et Control 1.2.4, puis relancer les applications audio qui utilisent encore l'ancienne DLL. Voir le [guide Windows](RELEASE_WINDOWS_1.2.4.md) et le [guide d'installation](INSTALL.md).
