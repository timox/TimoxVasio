# TimoxVasio 1.2.3

Cette version corrige l'arrêt du son lorsque Timox VASIO Control est fermé.

## Correction

- Control lance désormais le moteur comme processus Windows indépendant. Fermer la fenêtre laisse le moteur et l'audio actifs ; la commande explicite « Stop engine » conserve son rôle.
- Le lancement et la fermeture du processus sont couverts par un test de régression.
- La vue Channels ajoute « Quick routing » à côté de Connections et Matrix : choisir une source et une destination, puis ajouter une liaison mono ou stéréo avec une proposition de canaux disponibles. Les routes restent en brouillon jusqu'à « Apply configuration ».
- L'onglet Overview montre les canaux physiques utilisés ou disponibles et résume les flux par application. La liste des connexions enregistrées regroupe les routes par couple source/destination, condense les plages de canaux et indique si les changements sont appliqués.

## Installation

Télécharger les quatre fichiers de la [release 1.2.3](https://github.com/timox/TimoxVasio/releases/tag/v1.2.3). Installer le pilote et Control 1.2.3, puis relancer les applications audio qui utilisent encore l'ancienne DLL. Voir le [guide Windows](RELEASE_WINDOWS_1.2.3.md) et le [guide d'installation](INSTALL.md).

Après l'installation, fermer Control pendant une lecture audio pour vérifier que le son continue. Le moteur reste disponible sur son API locale ; utiliser « Stop engine » pour arrêter explicitement l'audio lorsque les clients ASIO sont fermés.
