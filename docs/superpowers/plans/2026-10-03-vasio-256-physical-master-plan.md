# TimoxVasio 256 canaux et horloge physique — plan d’implémentation

> **Pour les agents d’implémentation :** exécuter ce plan tâche par tâche dans cette session, en validant chaque tâche avant de passer à la suivante.

**Objectif :** remplacer VASIO1–VASIO4 à six canaux par l’unique pilote `TimoxVasio.dll` à 256 entrées et 256 sorties, et aligner strictement le transport virtuel sur la fréquence et la taille de bloc effectives du périphérique ASIO physique.

**Architecture :** `TimoxVasio.dll` conserve un mapping mémoire versionné par processus client et expose les canaux réellement alloués par `createBuffers`. Un seul hôte ASIO physique pilote l’horloge, la fréquence et la taille de bloc ; `TimoxVirtualAsioEngine.exe` recalcule les capacités dépendantes du taux et alloue les seuls canaux physiques routés. L’API documentée est l’unique contrat de configuration et d’inventaire pour le moteur et la GUI.

**Technologies :** C++20, SDK ASIO Steinberg fourni, Windows COM, CMake/Visual Studio 2026, cpp-httplib, JSON API v1, Electron/React.

**Spécification :** `docs/superpowers/specs/2026-10-03-vasio-single-driver-256-channel-design.md` et objectif/architecture de `docs/superpowers/specs/2026-10-02-virtual-asio-routing-design.md`.

## Contraintes globales

- Un seul pilote enregistré : `TimoxVasio`, avec 256 entrées et 256 sorties annoncées.
- Les index ASIO sont à base zéro ; les numéros de canaux d’API sont à base un.
- Chaque client expose seulement les entrées/sorties réellement demandées par `createBuffers`.
- Une application client ne peut demander qu’une fréquence et taille de bloc égales aux valeurs physiques actives.
- Le taux physique est fixé avant de relire les capacités susceptibles d’en dépendre.
- Un changement de configuration arrête le routage avant de modifier hôte, buffers ou graphe ; en cas d’échec, le moteur reste arrêté et publie l’erreur.
- Aucun callback audio ne fait d’allocation, d’accès disque ou d’attente sur un verrou bloquant.
- Aucun fichier hors du dossier `asio` ne peut être modifié ou inclus dans un commit.
- Les plans du 2 octobre 2026 pour drivers, moteur/API et graphe/GUI décrivent l’ancien modèle quatre pilotes/six canaux ; ce plan les remplace et ils ne doivent pas être exécutés tels quels.
- Le mapping 256/256 utilise 32 Mio de données d’anneaux float32 par client, hors buffers temporaires et buffers ASIO.

## Points de revue

- `createBuffers` reçoit des canaux clairsemés, dupliqués ou jusqu’à l’index 255 : tests de validation et transport bidirectionnel.
- Plusieurs clients possèdent des PID distincts et des canaux qui se recouvrent : tests d’identité, d’attachement et de détachement.
- Le pilote physique modifie ses comptes de canaux selon le taux : sonde vérifiant ordre set-rate, relecture des capacités et buffers.
- Le taux ou la taille client ne correspond pas au transport physique : rejet avant mutation du mapping ou du flux.
- Une route référence un port physique invalide ou non alloué : validation avant démarrage et moteur arrêté avec erreur structurée.

## Carte des fichiers

- `include/audio_transport.h`, `src/audio_transport.cpp` : disposition partagée, capacité, masques/listes de canaux actifs et version du protocole.
- `src/vasio_driver.cpp`, `src/vasio_driver_factory.cpp`, `src/vasio_com_driver.h` : contrat `IASIO`, allocation sparse et transport client.
- `src/vasio_client_manager.cpp`, `include/vasio_client_manager.h` : découverte de l’unique DLL, validation de version et snapshots de clients.
- `src/physical_asio_host.cpp`, `include/physical_asio_host.h`, `src/audio_controller.cpp`, `include/audio_controller.h` : négociation physique, capacités après taux, taille effective et buffers routés.
- `src/audio_routing_runtime.cpp`, `src/routing_graph.cpp`, leurs en-têtes et tests : traitement des canaux actifs et des index physiques réels.
- `src/control_api_server.cpp`, schémas OpenAPI, `API.md`, `gui/electron/*`, `gui/src/*` : contrat et GUI pilotés par API.
- `CMakeLists.txt`, scripts d’installation/désinstallation et probes sous `tests/` : une cible DLL, migration idempotente des anciens CLSID, vérification externe.
- `README.md`, `INSTALL.md`, `BUILD_DRIVERS.md`, `gui/GUI_GUIDE.md` et plans de livraison : documentation conforme au contrat unique.

## Tâches

### Tâche 1 : verrouiller le nouveau contrat de capacité et le transport

**Fichiers :** `include/audio_transport.h`, `src/audio_transport.cpp`, `tests/audio_transport_probe.cpp`.

- [ ] Écrire un probe qui transmet des blocs avec des canaux actifs 0, 127 et 255 dans les deux directions, et qui vérifie qu’un mapping de protocole antérieur est refusé.
- [ ] Exécuter le probe et vérifier qu’il échoue actuellement sur la capacité six canaux.
- [ ] Remplacer la capacité constante par 256 et vérifier que les canaux d’index élevé traversent les deux anneaux.
- [ ] Calculer les offsets d’anneau avec une capacité fixe 256 et conserver l’index de lecture sous propriété du consommateur.
- [ ] Incrémenter explicitement la version du mapping et refuser les dispositions ou versions incompatibles.
- [ ] Publier les canaux actifs avec le driver dans la tâche 2, où `createBuffers` est leur source et le test peut vérifier l’allocation ASIO de bout en bout.
- [ ] Rejouer les probes de transport, y compris wraparound, overrun et underrun, et vérifier l’absence de régression.

### Tâche 2 : faire annoncer et transporter 256/256 par l’unique DLL

**Fichiers :** `src/vasio_driver.cpp`, `src/vasio_driver_factory.cpp`, `src/vasio_com_driver.h`, `tests/driver_probe.cpp`, `tests/driver_audio_probe.cpp`.

- [ ] Étendre les probes pour exiger 256 entrées/sorties, accepter des allocations clairsemées jusqu’au canal 255 et refuser index 256, doublon même direction et plus de 512 structures.
- [ ] Observer leur échec avant modification du driver.
- [ ] Remplacer les bornes six canaux par la constante partagée ; valider séparément les indices d’entrée et de sortie.
- [ ] Publier dans le mapping partagé les canaux alloués par `createBuffers`, avec lecture cohérente par snapshot côté moteur.
- [ ] Construire les buffers client à partir des canaux réellement demandés sans créer de buffers pour les canaux non demandés.
- [ ] Adapter le transport intercalé afin que la position du canal ASIO conserve le bon index dans le mapping 256 slots.
- [ ] Exécuter probes unité et COM : valeurs getChannels, indices bas/haut, canaux clairsemés, doublons et transfert d’échantillons.

### Tâche 3 : réduire l’identité et l’attachement à un driver

**Fichiers :** `CMakeLists.txt`, `src/vasio_driver_factory.cpp`, `src/physical_asio_host.cpp`, `src/vasio_client_manager.cpp`, `include/vasio_client_manager.h`, tests de registre et client.

- [ ] Écrire/adapter le test de registre pour ne découvrir et instancier qu’un pilote `VASIO`, et pour vérifier que la migration retire les quatre anciens noms/CLSID documentés sans modifier d’autres pilotes.
- [ ] Vérifier l’échec actuel attendu avec les quatre pilotes codés en dur.
- [ ] Remplacer les quatre cibles/identités par un CLSID stable et un artefact x64 unique ; conserver les exports et l’enregistrement COM requis par l’énumérateur Steinberg.
- [ ] Détecter le seul module VASIO et attacher un client une fois par PID, avec vérification de l’identité du processus et de la version du mapping.
- [ ] Rendre install, migration, uninstall et clean idempotents et strictement limités aux CLSID VASIO connus.
- [ ] Compiler la DLL et passer les tests/probes d’enregistrement, d’énumération, d’instanciation et d’attachement enfant.

### Tâche 4 : aligner le moteur sur les paramètres effectifs du matériel

**Fichiers :** `src/physical_asio_host.cpp`, `include/physical_asio_host.h`, `src/audio_controller.cpp`, `include/audio_controller.h`, `tests/physical_asio_probe.cpp`.

- [ ] Ajouter au probe l’ordre de négociation : choisir le taux supporté, appeler `setSampleRate`, relire le taux et `getChannels`/`getBufferSize`, puis créer les buffers demandés.
- [ ] Faire échouer le test si les capacités post-taux ne sont pas celles utilisées ou si un canal non routé est alloué.
- [ ] Modifier l’ouverture/configuration pour publier uniquement le taux confirmé et choisir une taille autorisée par les capacités physiques du driver.
- [ ] Construire les buffers physiques à partir de l’union des canaux référencés par les routes, tout en conservant les index matériels exacts.
- [ ] Transmettre les mêmes valeurs effectives au runtime et au VASIO ; rejeter toute demande client de taux ou taille différente sans altérer le mapping actif.
- [ ] Vérifier les chemins `stopped`, succès et erreur ; toute erreur de reconfiguration laisse le moteur arrêté.

### Tâche 5 : rendre graph/API dynamiques et cohérents

**Fichiers :** `src/audio_routing_runtime.cpp`, `src/routing_graph.cpp`, `src/control_api_server.cpp`, schémas JSON/OpenAPI, `API.md`, tests de graph/runtime/API.

- [ ] Ajouter des cas couvrant les canaux actifs jusqu’à 256 et les canaux physiques au-delà de six, pour les liens virtuel→virtuel, virtuel→physique et physique→virtuel.
- [ ] Vérifier qu’ils échouent quand l’inventaire ou le runtime se limite aux quatre pilotes/six canaux.
- [ ] Construire les endpoints virtuels à partir des snapshots clients et canaux actifs, avec IDs uniques par PID et canal.
- [ ] Retirer les listes VASIO1–VASIO4 et les boucles `<= 6` du contrat API ; décrire un seul driver et les valeurs physiques confirmées.
- [ ] Ajouter les erreurs structurées pour fréquence/taille incompatibles, capacité invalide et reconfiguration échouée.
- [ ] Vérifier que la commande apply arrête le flux avant toute mutation, puis redémarre seulement après validation complète.
- [ ] Régénérer ou valider OpenAPI et exécuter l’ensemble des tests de graph, runtime et contrat API.

### Tâche 6 : adapter l’interface au contrat API unique

**Fichiers :** `gui/electron/api-client.js`, `gui/electron/main.js`, `gui/electron/preload.js`, `gui/src/App.js`, tests et documentation GUI.

- [ ] Mettre à jour les fixtures de contrat pour le driver unique, les endpoints actifs par client, et la fréquence/taille physiques effectives.
- [ ] Vérifier d’abord que les tests UI échouent si l’interface rend quatre drivers ou des réglages virtuels indépendants.
- [ ] Afficher l’inventaire physique et les clients/endpoints VASIO fournis par l’API ; soumettre toute modification via `configuration.apply`.
- [ ] Retirer les données métier locales et toute configuration indépendante de taux/taille VASIO.
- [ ] Vérifier les trois catégories de route, les transitions d’interruption et les erreurs API par tests UI et build React/Electron.

### Tâche 7 : intégrer, documenter et vérifier sur hôte réel

**Fichiers :** scripts, `README.md`, `INSTALL.md`, `BUILD_DRIVERS.md`, `API.md`, documentation GUI et probes de bout en bout.

- [ ] Mettre à jour les guides pour une DLL 256/256, la migration des anciens noms, le coût mémoire exact par client et le verrouillage fréquence/taille par le matériel.
- [ ] Compiler proprement en x64 avec l’environnement Visual Studio 2026 et exécuter les probes ciblés puis la suite complète définie par CMake/GUI.
- [ ] Installer la DLL unique et vérifier dans un énumérateur ASIO qu’elle expose 256 entrées/sorties et peut allouer des canaux haut/clairsemés.
- [ ] Vérifier avec hôte ASIO réel les valeurs physiques annoncées, le rejet des désaccords de paramètres et les trois flux audio mesurables.
- [ ] Vérifier installation répétée, migration depuis VASIO1–VASIO4, redémarrage moteur, déconnexion/reconnexion et état d’erreur arrêté.
- [ ] N’annoncer la livraison complète qu’après preuve des critères d’acceptation de la spécification et mise à jour du ledger.

## Auto-revue du plan

- Couverture : les tâches 1–2 couvrent capacité, layout partagé et allocations client ; 3 couvre identité, COM et migration ; 4 couvre fréquence/taille/canaux physiques ; 5 couvre graphe/API/arrêt ; 6 couvre GUI ; 7 couvre documentation, compilation et vérification réelle.
- Revue des entrées limites : index 255/256, canaux clairsemés/dupliqués, client simultané, capacité matérielle variant selon taux, taille/taux non compatibles, endpoint matériel disparu et version mémoire obsolète sont explicitement affectés aux probes des tâches propriétaires.
- Cohérence des interfaces : le mapping partagé définit la capacité dans la tâche 1 ; le driver et le manager consomment cette version aux tâches 2–3 ; le contrôleur publie les paramètres effectifs de la tâche 4 ; graph, API et GUI utilisent ensuite les snapshots et valeurs confirmées.
- Portée : aucun fichier d’un projet voisin n’est dans la carte des fichiers ou les étapes Git. Les opérations de runtime ASIO réel sont réservées à la vérification finale après build et installation documentés.
