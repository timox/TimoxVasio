# TimoxVasio — synthèse du projet

Le dépôt public dédié est [TimoxVasio](https://github.com/timox/TimoxVasio). Ce dossier `asio` du dépôt `grrzzzz` conserve un lien vers ce projet et les éléments de travail qui y sont migrés.

## Objectif

Fournir un pilote ASIO virtuel Windows x64 unique, `TimoxVasio`, qui expose jusqu’à 256 canaux d’entrée et 256 canaux de sortie à chaque application compatible ASIO. Un moteur distinct relie les canaux effectivement ouverts par les applications aux entrées et sorties d’un pilote ASIO physique. Une interface configure le périphérique maître et les routes.

## État actuel

- La DLL `TimoxVasio.dll` et le moteur `TimoxVirtualAsioEngine.exe` sont deux composants distincts; les builds sont documentés dans [BUILD_DRIVERS.md](BUILD_DRIVERS.md).
- La sonde COM confirme 256 canaux dans chaque direction, jusqu’à l’index 255. Des probes couvrent aussi l’allocation de buffers, le transport et le graphe de routage.
- Le moteur ouvre le pilote ASIO physique choisi et utilise sa fréquence et sa taille de bloc effectives comme référence commune.
- L’API publie les clients et les canaux réellement alloués; la GUI transmet les changements de configuration à cette API.
- La découverte de `TimoxVasio` a été confirmée avec une build locale modifiée de Mixxx. Le test de bout en bout du signal routé a également été réalisé et confirmé par l’utilisateur le 5 octobre 2026; les détails de mesure ne sont pas consignés dans ce README.

Les séquences ASIO, les profils API et le chemin Mixxx sont illustrés dans [DRIVER_SEQUENCES.md](docs/DRIVER_SEQUENCES.md). L'architecture est résumée dans [ARCHITECTURE.md](ARCHITECTURE.md). Les preuves et limites du contrôle de protocole sont résumées dans [ASIO_CONFORMITE.md](ASIO_CONFORMITE.md). La [revue UX/UI](docs/UX_UI_REVIEW.md) documente l'interface, les corrections de lisibilité et les preuves visuelles. Les décisions détaillées sont dans la [spécification](docs/superpowers/specs/2026-10-03-vasio-single-driver-256-channel-design.md) et le [plan de réalisation](docs/superpowers/plans/2026-10-03-vasio-256-physical-master-plan.md).

Le récapitulatif de cette version est dans [RELEASE_NOTES_v1.0.0.md](docs/RELEASE_NOTES_v1.0.0.md).
Les travaux prévus après la release 1.1.0 figurent dans la [feuille de route](docs/ROADMAP.md).

## Utiliser l’API et soutenir le projet

Le [quick start API](docs/API_QUICKSTART.md) présente l’architecture complète
et des exemples PowerShell/Node.js. Le contrat détaillé se trouve dans
[`API.md`](API.md) et [`openapi-v1.json`](openapi-v1.json).

TimoxVasio peut être soutenu via [GitHub Sponsors](https://github.com/sponsors/timox).
Voir [Soutenir le projet](docs/FUNDING.md) pour savoir comment le financement
sera utilisé.

## Architecture simplifiée

```mermaid
flowchart LR
    subgraph Apps[Applications compatibles ASIO]
        A["Application A<br/>canaux alloués"]
        B["Application B<br/>canaux alloués"]
    end
    D["TimoxVasio.dll<br/>256 entrées + 256 sorties"]
    M["TimoxVirtualAsioEngine.exe<br/>transport et graphe"]
    P["Pilote ASIO physique<br/>horloge maître"]
    UI[Interface React / Electron]
    API[API de contrôle]

    A <-->|ASIO| D
    B <-->|ASIO| D
    D <-->|transport audio partagé| M
    M <-->|"ASIO<br/>fréquence et bloc effectifs"| P
    UI <-->|configuration et état| API
    API <--> M
```

Le pilote virtuel annonce sa capacité maximale. Chaque application ne rend disponibles que les canaux qu’elle a effectivement alloués. Le moteur construit les routes depuis ces canaux et l’inventaire réel du périphérique physique. Le détail du contrat figure dans la [spécification](docs/superpowers/specs/2026-10-03-vasio-single-driver-256-channel-design.md).

## Vérifications

Les builds et probes locaux valident des parties du pilote, du transport et du moteur. Le test dans un hôte tiers avec routage audio de bout en bout a été réalisé et confirmé par l’utilisateur le 5 octobre 2026. Les résultats détaillés et mesures chiffrées ne sont pas reproduits ici.

Voir [BUILD_DRIVERS.md](BUILD_DRIVERS.md) pour reconstruire et sonder les composants, et [INSTALL.md](INSTALL.md) pour l’installation.

## Licence

Le code original est sous GNU GPL version 3 (`GPL-3.0-only`). Les licences ASIO SDK et des dépendances tierces gardent leurs propres termes. Le nom du pilote reste **TimoxVasio**. Voir [LICENSING.md](LICENSING.md).

## Les trois composants

- **TimoxVasio** : pilote ASIO virtuel chargé par les applications audio.
- **TimoxVirtualAsioEngine** : moteur qui transporte et route l’audio entre les applications et le pilote physique.
- **Timox VASIO Control** : interface Electron qui configure le moteur, ses routes et ses diagnostics.

L’interface et le moteur communiquent via l’API documentée. Le nom du pilote reste `TimoxVasio`; le nom de l’application Electron est `Timox VASIO Control`.
