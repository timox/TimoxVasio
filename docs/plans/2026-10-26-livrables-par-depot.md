# Livrables par dépôt — constat du 26 octobre 2026

La séparation définit quatre producteurs : pilote ASIO, moteur/API, interface de contrôle, transport PWAR. **TimoxVSoundSystem désigne la suite**, pas un dépôt supplémentaire. **WASAPI reste un module de TimoxVAudioEngine** ; aucun dépôt ni livrable autonome WASAPI n'a été créé.

Ce constat distingue les sources canoniques des artefacts réellement présents. Les chemins relatifs ci-dessous partent de TimoxVasio, sauf dans la colonne « sortie canonique ». Aucun build, lancement, installation ou changement de configuration audio n'a été effectué pour cet inventaire.

## Répartition

| Dépôt producteur | Livrable et sortie canonique dans ce dépôt | Consommateurs et contenu | Exclus et qualification |
|---|---|---|---|
| **TimoxVasio** | `build_driver_110/Release/TimoxVasio.dll` ; distribution manuelle historique `gui/dist/TimoxVasio Driver 1.2.7.zip` ; sources du contrat de transport/ABI | Applications ASIO chargent la DLL ; TimoxVAudioEngine consomme un instantané épinglé du transport partagé. Contrat : `include/audio_transport.h`, `src/audio_transport.cpp`, `src/audio_transport_layout.h`, lecteur de statut et version associée. | Aucun nouveau pilote construit pour le lot API/GUI. Le dépôt contient encore les anciennes sources moteur/GUI et leur packaging ; l'extraction n'est pas une suppression de ces sources. ZIP/DLL existants ne constituent pas une nouvelle qualification avec le moteur récent. |
| **TimoxVAudioEngine** | `build/Release/TimoxVirtualAsioEngine.exe`, accompagné de `openapi-v1.json` et `schemas/` ; documentation sous `docs/` | TimoxVSSC et clients HTTP/WebSocket. Moteur : routage, persistance technique, horloges, ASIO physique, ponts WASAPI, API et supervision du client PWAR. Le nom de l'EXE reste compatible avec la découverte existante. | N'inclut ni installation de DLL ASIO ni GUI ni service/pilote natif autonome. `PwarAsioClient.exe` est une entrée externe vérifiée, placée à côté du moteur. Les tests isolés ne qualifient pas le matériel ou la continuité audio. |
| **TimoxVSSC** | `dist/TimoxVSSC-0.1.0-preview.exe`, EXE portable Windows | Utilisateur de la GUI Electron/React ; embarque moteur et client PWAR sous `resources/backend`, OpenAPI, schémas et notices. `artifacts.lock.json` impose révisions et SHA-256 ; préflight/hook/postflight contrôlent les copies. API requise : 1.1. | Preview portable, sans installateur ni installation/enregistrement du pilote ASIO. `dist/win-unpacked` est une copie de contrôle. Portable construit et contenu extrait vérifié ; lancement réel non testé. |
| **Timox-Pwar** | Windows : `build/Release/PwarAsioClient.exe`. Linux : sources et cibles `pwar_cli` / `pwar_stream_cli`. Contrat : `client/CONTROL.md` ; qualification et intégration documentées | Moteur supervise le client Windows par le contrôle stdio v1 ; GUI distribue le binaire épinglé. Sources transport, backends PipeWire, dépendances et licences conservées. | Pas de paquet Linux construit/qualifié dans cette extraction. L'ancienne DLL PWAR est optionnelle et désactivée par défaut, pas le pilote TimoxVasio. Audio PWAR **non qualifié stable** : 70 871 trames nulles / 480 000, 116 992 trames rejetées côté Linux. |

## Emplacement actuel des travaux et des builds

Les voisins extraits ont été observés aux commits `../TimoxVAudioEngine` **1798297**, `../TimoxVSSC` **cf388c4** et `../Timox-Pwar` **5a0f13e**, sans modification affichée par `git status --short`. TimoxVasio est à **3566e3a**, avec de nombreuses modifications utilisateur préexistantes conservées.

**Mise à jour du 27 octobre :** les évolutions API/supervision et GUI sont intégrées
dans les deux dépôts voisins, sur leurs branches
`api/supervision-20261026`, commitées et poussées : moteur `fb7113d`, GUI
`6d882df`. PWAR `5a0f13e` est poussé sur `migration/extraction-20261026`.
Les commits d'extraction cités ci-dessus décrivent l'état antérieur.
114 fichiers sources/artefacts ont été transférés avec sauvegardes et contrôle
SHA-256. Les copies de préparation sont conservées sous
`build_gui_api_20261026/TimoxVAudioEngine` et
`build_gui_api_20261026/release/TimoxVSSC`. Le lock GUI référence le manifeste
moteur actuel et ses binaires, pas le seul commit historique.

### Livrables du deuxième lot

- GUI : `../TimoxVSSC/dist/TimoxVSSC-0.1.0-supervision2.exe`, **71 570 404 octets**,
  SHA-256 `414e0d0132a3a92785731fac2e6f84f898eb847f4d49ed4882b507dc5817a034`.
- Moteur : `../TimoxVAudioEngine/build/Release/TimoxVirtualAsioEngine.exe`,
  **1 509 376 octets**, SHA-256
  `da90a3c24d2bbb601ab259e0f531bdf78089b721a38e2715c122a5757bb6aded`.
  OpenAPI et schémas correspondants sont fournis à côté.
- Pilote et client PWAR : aucun nouveau binaire pour ce lot ; versions et hashes
  ci-dessous conservés. WASAPI reste inclus au moteur.

Corrections supplémentaires : observations WASAPI conservées même sans endpoint
résolu, état demandé distinct de l'état observé, association PWAR au PID courant
du superviseur, inconnus et déconnexions rendus explicitement.
Validation : 35 tests React ciblés et 35 tests Node/Electron/contrat passent ;
deux suites C++ passent ; réponses HTTP/WS et observation non résolue validées
contre JSON Schema. Le portable a été extrait : moteur, PWAR, schémas,
application et Swagger correspondent aux entrées vérifiées. Après transfert,
162 entrées de provenance moteur et 5 fichiers ABI épinglés sont conformes,
ainsi que le contrôle des artefacts GUI empaquetés.

La GUI et le moteur déjà ouverts n'ont pas été redémarrés. Le nouvel EXE n'a
pas été lancé ; aucune nouvelle qualification audio matérielle/réseau.

### Artefacts du premier lot, conservés

| Artefact observé | Chemin actuel | Taille | Modification UTC |
|---|---|---:|---|
| Portable GUI récent | `build_gui_api_20261026/TimoxVSSC/dist/TimoxVSSC-0.1.0-preview.exe` | 71 563 652 octets | 2026-10-26 21:13:50.1079816Z |
| Moteur embarqué dans le premier portable | `build_gui_api_20261026/TimoxVSSC/artifacts/backend/TimoxVirtualAsioEngine.exe` | 1 508 352 octets | 2026-10-26 20:43:45.7623876Z |
| Client PWAR fourni à la GUI | `build_gui_api_20261026/TimoxVSSC/artifacts/backend/PwarAsioClient.exe` | 151 552 octets | 2026-10-26 18:40:42.3855373Z |
| Pilote existant | `build_driver_110/Release/TimoxVasio.dll` | 162 816 octets | 2026-10-24 13:03:55.3187997Z |
| ZIP pilote existant | `gui/dist/TimoxVasio Driver 1.2.7.zip` | 87 473 octets | 2026-10-24 13:04:13.3524209Z |

SHA-256 calculés directement pendant cet inventaire :

| Artefact | SHA-256 |
|---|---|
| Portable GUI | `75c7ee198dedc5aece4459f61d89d36a3cc9be7ef21a38230808706b7f5c7ae4` |
| Moteur récent | `a8f52830e161699607cdcac44b5499c28456f71c2417a1977d91ef63c18210b5` |
| Client PWAR | `29d5e3208e10ae5febbb7fd3914790a87eaf12d32be6bef923f20d7e33566fd3` |
| Pilote existant | `f49db994bdd9eec43c628d7803dd57ac378b9d9c9caedebe97c208fce8c6deea` |
| ZIP pilote existant | `ece04ef250a163bf9aa46aca65c5e9c73e5162d0f6703771f3183f975b40e915` |

Les copies moteur et PWAR déjà extraites du portable sous `build_gui_api_20261026/TimoxVSSC/verification/portable/resources/backend/` ont les mêmes SHA-256 que ci-dessus. Leur OpenAPI a le SHA-256 `208dc9c42be9eb0b9449ad89b612c0ecf399c2c478b5848ccc1798e4a7633073`, égal au pin du lock. Le schéma API est épinglé dans ce lock à `da1980f6d1b79eeaf3744d0cd8ba97add372decd8b85cc32277a5afaf66fd345`.

Le moteur initial de l'extraction, documenté dans `../TimoxVAudioEngine/verification-results.json`, avait le SHA-256 `6cda07b551b1235264993281ffadf769fbdc04711b0f586751ccfe319045e29d`. Ce n'est **pas** le moteur du portable récent. Les migrations initiales décrivent parfois seulement le packaging répertoire : elles ne remplacent pas l'inventaire des artefacts récents.

## Preuves et limites

- Pilote : [AGENTS.md](../../AGENTS.md), [CMake du pilote](../../cmake/CMakeLists_VASIO_Drivers.txt) et chemins existants ci-dessus. Le Setup historique `gui/dist/Timox VASIO Control Setup 1.2.7.exe` appartient à l'ancien packaging global ; il n'est pas le livrable de TimoxVSSC. L'inventaire n'a pas extrait le ZIP pilote ni le Setup pour comparer leurs DLL : leur cohérence complète doit être vérifiée avant toute nouvelle distribution.
- Moteur : `../TimoxVAudioEngine/AGENTS.md`, `README.md`, `CMakeLists.txt`, `MIGRATION.md`, `verification-results.json`. Contrat partagé épinglé dans **`vendor/timoxvasio/dependency-lock.json`** : les copies ABI doivent être mises à jour depuis TimoxVasio, pas modifiées indépendamment. Résultat documenté de l'extraction initiale : 30/30 CTest, sans qualification matérielle ; ne pas attribuer automatiquement ce résultat au staging API récent.
- GUI : `../TimoxVSSC/package.json`, `MIGRATION.md`, scripts de vérification et lock récent dans `build_gui_api_20261026/TimoxVSSC/artifacts.lock.json`. L'extraction documente 34 tests Node réussis ; les quatre suites React sélectionnées avaient 35 réussites / 38 échecs, identiques à la baseline. Ces résultats historiques ne remplacent pas les vérifications du lot courant. Le profil `%APPDATA%/timox-vsounddealer` et son verrou restent partagés ; coexistence réelle non exercée.
- PWAR : `../Timox-Pwar/CMakeLists.txt`, `MIGRATION.md`, `client/CONTROL.md`, `integration/README.md`. Résultat Windows documenté : 21/21 tests isolés ; binaire présent aussi sous `../Timox-Pwar/build/Release/`. Les champs de latence/dérive non mesurés restent nuls. Aucun nouveau binaire PWAR produit par le lot API/GUI.

## Natif Windows expérimental

Le contenu [native-windows-audio](../../native-windows-audio/README.md) reste un chantier séparé dans TimoxVasio : prototype PortCls/WaveRT, contrat PCM/IOCTL, broker et outils de qualification. Il ne constitue ni un nouveau dépôt créé, ni le pilote ASIO, ni le module WASAPI, ni une livraison de production.

Le catalogue noyau est non signé ; chargement pilote, provisioning/enrôlement, service déployé, isolation entre sessions et échanges audio réels restent à qualifier. Les sources de canal déjà liées au moteur ne signifient pas que le service/pilote natif soit embarqué ou fonctionnel. Ne pas transformer les probes ou bibliothèques statiques de test en livrables utilisateur annoncés.

Restent hors de ce constat : exécution du portable, essai matériel, installation/mise à jour, signature/publication et nouvelle qualification audio PWAR. Aucune preuve de production n'est déduite d'un hash identique ou d'un test isolé réussi.
