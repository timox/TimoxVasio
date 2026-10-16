# TimoxVasio 1.2.2

Cette version améliore l'interface de routage et ajoute des exemples de pilotage par l'API locale.

## Changements

- La vue Channels affiche plus clairement les routes et leur état appliqué, avec une vue synthétique en complément de la matrice complète.
- La console Swagger intégrée résout les références du schéma OpenAPI hors ligne.
- Un exemple web, un Tool Renoise et une chaîne SuperCollider illustrent l'utilisation de l'API et le routage entre applications. Le Tool inclut un pont de transport OSC et un exemple d'horloge MIDI.
- La documentation a été réorganisée ; les notes des versions précédentes restent accessibles.
- Le script de création de l'archive pilote embarque à nouveau les bons fichiers d'installation.

## Installation

Télécharger les quatre fichiers de la [release 1.2.2](https://github.com/timox/TimoxVasio/releases/tag/v1.2.2). Installer le pilote et Control de la même version. Relancer les applications audio qui ont encore une ancienne DLL chargée. Voir le [guide Windows](RELEASE_WINDOWS_1.2.2.md) et le [guide d'installation](INSTALL.md).

Le routage audio et l'horloge MIDI des exemples doivent être configurés selon les applications et périphériques présents sur la machine ; les exemples ne synchronisent pas automatiquement tous les DAW.
