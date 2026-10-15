# TimoxVasio 1.2.0

Cette version réduit le travail effectué dans le callback audio lorsque l'application ASIO ouvre peu de voies. La capacité annoncée reste de 256 entrées et 256 sorties par application.

## Changements

- Le transport partagé copie uniquement les voies créées par `createBuffers`, dans l'ordre de leurs numéros. Son format v8 coordonne la DLL et le moteur lors des reconfigurations et rejette les anciens formats.
- Les copies des anneaux sont effectuées par portions contiguës. Le callback physique évite un second effacement des sorties déjà initialisées.
- Les clients sans route continuent de recevoir leurs cycles. La suppression de la dernière route laisse les sorties physiques silencieuses.
- L'API documentée, le tampon et les réglages de phase ne changent pas.

## Mesures et vérifications

Avec un client à une sortie, une route muette, SSL ASIO Driver 1 à 48 kHz et 1 024 trames, la médiane des pires temps de traitement du callback physique sur les fenêtres de cinq secondes est passée de **626,9 µs à 43,8 µs**, soit environ **14 fois moins** entre l'ancienne paire DLL/moteur et la paire 1.2.0. Aucun retard ni dépassement du callback physique n'a été déclaré dans ces passages. Le détail des quatre passages alternés figure dans [le rapport de mesures](archive/MESURES_1_1_1_TEST_2026-10-11.md).

Les probes Release couvrent le transport interprocessus, 0, 2, 8 et 256 voies, les canaux clairsemés, le cycle de vie de la DLL, la reconfiguration, le silence sans route et la progression des clients. Les builds DLL et moteur de cette publication sont produits ensemble et leurs empreintes figurent dans `MANIFEST.json`.

## Téléchargements Windows x64

| Fichier | Contenu |
| --- | --- |
| `TimoxVasio.Driver.1.2.0.zip` | Pilote ASIO, installateur et script d'enregistrement. |
| `Timox.VASIO.Control.Setup.1.2.0.exe` | Installateur de l'interface et du moteur. |
| `Timox.VASIO.Control.1.2.0.exe` | Interface portable et moteur. |
| `MANIFEST.json` | Commit, versions et empreintes SHA-256. |

Installer le pilote depuis le ZIP, puis Control. Les applications audio déjà ouvertes doivent être relancées pour charger la nouvelle DLL. Le guide [Installation](INSTALL.md) décrit les étapes.
