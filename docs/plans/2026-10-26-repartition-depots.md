# Répartition des dépôts — plan d'action

> Plan historique d'extraction. Pour le modèle actuel, lire la
> [spécification adhérents/nœuds/supervision](../superpowers/specs/2026-10-26-adherents-noeuds-supervision-design.md).
> Elle remplace notamment les passages attribuant l'identité du graphe à la GUI.
> Les livrables et leur état sont suivis dans [l'inventaire par dépôt](2026-10-26-livrables-par-depot.md).

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Séparer pilote, moteur, contrôle et transport PWAR sans perdre les travaux locaux ni casser les contrats existants.

**Architecture:** Extraire d'abord les composants à comportement constant, puis faire évoluer le modèle validé. TimoxVSSC possède les identités du graphe ; TimoxVAudioEngine possède les ressources techniques et leur état appliqué/observé. Les décisions ouvertes ci-dessous restent des propositions.

**Tech Stack:** C++/CMake, ASIO et WASAPI Windows, Electron/React, API JSON/OpenAPI, PWAR/PipeWire Linux.

## 1. État vérifié et portée

Plan établi le 26 octobre 2026 avec trois sous-agents en lecture seule : moteur/pilote, GUI et PWAR.

- TimoxVasio : branche `ccr-d306084d-ieaii9`, HEAD `3bcc05f` (reprise déjà commitée), nombreux fichiers modifiés et non suivis. Ne pas rejouer la demande historique de commit du fichier de reprise.
- TimoxVAudioEngine : branche `main`, commit `d4cd93a`, état Git local propre.
- TimoxVSSC et Timox-Pwar : clones présents, sans commit local. Aucun accès distant vérifié pendant cette planification.
- Seul TimoxVasio est actuellement une racine autorisée en écriture dans cette session. Avant exécution multi-dépôts, rendre les dépôts cibles accessibles en écriture et lire leurs consignes ; les droits décrits par une ancienne reprise ne prouvent pas les droits actuels.
- Le présent travail autorise la planification, pas les builds, essais audio, installations, publications ou push.

Références : `REPRISE_2026-10-26.md`, `AGENTS.md`, `docs/superpowers/specs/2026-10-26-timox-object-model-design.md`, `PWAR_REPRISE.md`, `PWAR_SUBAGENT_TASK.md`, `patches/pwar/REPLY_TIMOX.md`, `patches/pwar/client/CONTROL.md`.

Les échanges PWAR récents remplacent la coordination par boîte aux lettres Markdown : utiliser un seul sous-agent PWAR et les messages inter-agents, avec le parent responsable du moteur partagé. Les anciens PID, ports libres et autorisations ponctuelles ne valent pas constat actuel.

## 2. Répartition proposée

| Dépôt | Responsabilité | Sources actuelles principales | Livrable |
|---|---|---|---|
| TimoxVasio | Pilote ASIO, identité COM, compatibilité applications, contrat mémoire partagé | `src/vasio_driver*`, `src/vasio_com_driver.h`, `src/vasio_compatibility_profile.*`, `src/driver_version.*`, `cmake/CMakeLists_VASIO_Drivers.txt` | DLL, ZIP pilote, contrat partagé versionné |
| TimoxVAudioEngine | Horloges, carte physique, routage, sessions, persistance et API technique | `src/audio_controller.cpp`, `src/audio_routing_runtime.cpp`, `src/routing_graph.cpp`, `src/physical_asio_host.cpp`, `src/software_clock_host.cpp`, `src/vasio_client_manager.cpp`, `src/control_api_server.cpp`, `src/audio_configuration_store.*`, headers et tests associés, `api/` | Moteur et schémas API issus de la même révision |
| TimoxVSSC | GUI, identité `VSSCClient`, correspondance vers les ressources techniques, orchestration utilisateur | `gui/src/`, `gui/electron/`, `gui/public/`, `gui/assets/`, `gui/scripts/`, package et lockfile | Application de contrôle ; packaging intégré à définir explicitement |
| Timox-Pwar | Client ASIO PWAR, transport réseau, backend Linux, contrat de contrôle et qualification | `patches/pwar/client/`, patches, tests, scripts et sources consolidées vérifiées | Client Windows, sources Linux reproductibles, contrat `CONTROL.md` |
| TimoxVWasapi, si retenu | Bibliothèque WASAPI réutilisable | Backend, inventaire, notifications, conversions et ponts à délimiter | Bibliothèque ; aucun service séparé implicite |

TimoxVSoundSystem reste le nom de la suite : aucun dépôt supplémentaire à créer maintenant.

Cas transversaux à préserver :

- `src/audio_transport.cpp`, `src/audio_transport_layout.h`, `include/audio_transport.h` sont partagés par le pilote et le moteur. Proposition : source canonique dans TimoxVasio et dépendance du moteur épinglée sur une révision. Choisir le mécanisme de consommation avant extraction ; ne pas entretenir deux copies divergentes.
- `src/application_profile_store.cpp` fournit la lecture partagée ; `src/application_profile_store_write.cpp` reste côté contrôle/moteur. Ne pas ajouter d'écriture de configuration dans la DLL.
- Les parties déjà intégrées de l'audio natif suivent leurs consommateurs ; le broker expérimental de `native-windows-audio/` ne doit pas devenir une dépendance de production par déplacement de dossier. Inventorier séparément ce qui reste expérimental.
- `src/pwar_client_supervisor.cpp` et `include/pwar_client_supervisor.h` sont déjà raccordés au moteur/API. Proposition pour la première extraction : les conserver dans le moteur ; TimoxVSSC pilote cette API, Timox-Pwar possède le processus client. Valider ce choix avant changement de responsabilité.

## 3. Décisions à valider avant migration

### D1 — WASAPI : module ou dépôt autonome

| Option | Conséquence concrète | Coût |
|---|---|---|
| Module du moteur | Même processus et cycle de version ; accès direct aux ressources du graphe | Extraction minimale, changements atomiques moteur/ponts |
| Dépôt autonome, bibliothèque liée au moteur | Version et tests propres ; le moteur ouvre toujours les streams | Interface à stabiliser, dépendance épinglée, compatibilité à tester |
| Service autonome | Autre processus possédant les streams | Transport audio interprocessus, synchronisation, supervision et latence à qualifier |

Un dépôt autonome n'impose pas un service. Le couplage actuel de `include/wasapi_bridge_manager.h` avec `audio_routing_runtime.h` signifie qu'il faut conserver l'adaptateur du graphe dans le moteur ou définir une interface adaptée. Préserver arrêt des callbacks, publication et durée de vie des buffers.

Recommandation provisoire : module pour une extraction rapide ; bibliothèque autonome si la réutilisation et les versions indépendantes sont un objectif explicite. Aucune option n'est décidée par ce plan.

### D2 — Modèle et autorité

Valider ensemble :

1. `AudioConfiguration` = souhaité/persisté ; `AudioEngine` = appliqué/observé ; inventaire = observation référencée.
2. `VSSCClient` appartient à TimoxVSSC ; distinguer ASIO attaché, endpoint WASAPI, process-copy et PWAR, sans confondre rôle produit et transport.
3. Un processus copié est le périmètre source, pas un client ASIO. Les callbacks ne prouvent pas un signal audible.
4. L'API technique reste au moteur. Définir si « API unifiée » désigne une façade TimoxVSSC ou seulement une présentation unifiée ; ne pas créer un deuxième propriétaire de la même configuration.
5. Définir les révisions souhaitée/appliquée, la persistance éventuelle de révision et la règle des mutations concurrentes. Aujourd'hui `expectedConfigurationRevision` est optionnel et aucune révision appliquée distincte n'est publiée.
6. Trancher le stockage PWAR, l'ownership des profils applications, l'identité des sessions au-delà du PID, la quantité d'inventaire dans le snapshot et le placement du superviseur PWAR.

Livrable : mise à jour de la proposition de modèle avec décisions acceptées et questions encore ouvertes. Ne pas modifier l'API pour anticiper une réponse.

## 4. Lots exécutables et critères de sortie

### Lot 0 — Préserver et inventorier

Responsable : parent/intégration. Prérequis : aucun.

1. Dans chaque dépôt, lire `AGENTS.md`, confirmer la racine par `git rev-parse --show-toplevel` et lire `git status --short --branch`.
2. Dans TimoxVasio, inventorier séparément fichiers suivis modifiés et non suivis avec `git diff --name-status` et `git ls-files --others --exclude-standard`.
3. Créer un manifeste de migration : chemin source, dépôt et chemin cible, responsable, commit source, SHA-256 des sources locales, tests associés, dépendances et statut. Inclure callers, scripts, fixtures, documentation, licences et configuration CI.
4. Préserver les changements locaux dans des lots explicites et vérifiables. Aucun `git add -A`, reset, nettoyage global ni remplacement de fichier non suivi.
5. Identifier les sources qui existent seulement sous un dossier de build, notamment PWAR ; les comparer aux patches avant de définir une source canonique.

Sortie : chaque fichier à migrer est attribué, les modifications locales sont récupérables et aucune donnée utilisateur n'est incluse dans un export public.

### Lot 1 — Valider les frontières

Responsable : parent avec l'utilisateur. Dépendance : lot 0.

1. Présenter D1 et D2 avec leurs coûts et frontières.
2. Consigner les décisions dans la spécification du modèle.
3. Définir la dépendance partagée pilote/moteur et une matrice de compatibilité DLL, moteur, API, GUI et protocole PWAR.
4. Fixer le périmètre du packaging de TimoxVSSC : remplacement du Setup intégré ou prévisualisation séparée. Conserver l'installation du pilote et la distinction Setup/portable de la distribution actuelle jusqu'à validation d'un remplacement.

Sortie : les responsabilités et formats nécessaires au premier déplacement sont acceptés. Les fonctionnalités futures sans décision restent hors extraction.

### Lot 2 — Extraire moteur et frontière pilote

Responsable : sous-agent moteur/pilote. Dépendance : lot 1.

1. Importer dans TimoxVAudioEngine les sources, headers, tests, schémas et notices attribués par le manifeste, en conservant leur provenance.
2. Adapter les cibles CMake à la dépendance partagée versionnée ; conserver l'ABI et l'API existantes. Garder provisoirement le nom technique de l'exécutable si son renommage n'est pas nécessaire à l'extraction.
3. Déplacer WASAPI selon D1 et préserver les dépendances des tests du contrôleur/API.
4. Conserver l'accès en lecture aux profils côté pilote et l'écriture côté moteur.
5. Lors de l'exécution autorisée, construire depuis un clone autonome et lancer les tests existants concernés : persistance, horloge, routage/hot-swap, instance unique, WASAPI et contrat API. Vérifier aussi `DriverCompatibilityProfileTests` et `tests/verify_driver_boundary.ps1` côté pilote.

Sortie : pas de chemin de build implicite vers l'ancien clone ; DLL et moteur compatibles ; configuration, horloge et routes conservées. Aucun essai matériel déclenché par les seuls tests de migration.

### Lot 3 — Extraire TimoxVSSC

Responsable : sous-agent GUI. Dépendance : contrats du lot 1 ; validation intégrée après lot 2.

1. Importer la GUI complète avec Electron, scripts, ressources, tests et lockfile ; ne pas importer seulement `gui/src/`. Exclure `dist`, `build` et `node_modules` de l'import des sources.
2. Renommer les textes et métadonnées TVSD/SoundDealer vers TimoxVSSC dans le nouveau dépôt. Traiter explicitement l'identité d'application, le répertoire de données, les préférences et secrets existants pour éviter une perte de configuration.
3. Adapter `electron/main.js`, `electron/engine-discovery.js`, `scripts/embed-openapi.js`, `package.json`, `electron-builder.tvsd.json` et les scripts de vérification aux artefacts épinglés du moteur et de PWAR.
4. Remplacer la compatibilité API déduite de la version produit par une règle explicite ; conserver la protection contre un deuxième moteur et les contrôles d'authentification.
5. Exécuter les tests de contrat et React pertinents : `npm run contract-test`, puis les tests App, RoutingPatchbay, WasapiDeviceTree et PwarDialog. Inclure les tests existants Electron api-client, engine-discovery, engine-process, main-startup et embed-openapi. Vérifier la coexistence ancienne/nouvelle GUI après changement d'identité. Ajouter un test ciblé si une règle de compatibilité ou de migration des données change.
6. Vérifier l'interface aux tailles usuelles : navigation gauche globale seulement ; actions d'objet par menu contextuel ; formulaires modaux ; sélectionner un câble n'ouvre rien ; aucun panneau permanent de propriétés.

Sortie : GUI utilisable avec le moteur ciblé, mêmes fonctions et données, pas de dépendance au chemin `../build_codex_110`. La preview6 reste une référence de travail, pas une release validée.

### Lot 4 — Extraire et qualifier PWAR

Responsable : un seul sous-agent PWAR ; parent pour moteur/API/GUI. Dépendance : lot 1 pour extraction ; lots 2/3 pour intégration.

1. Réconcilier `patches/pwar/`, les scripts de bundles et `build_pwar_reconciled_20261025` ; importer une source Linux/Windows reproductible dans Timox-Pwar, avec provenance upstream et ordre des patches. `qualification-inputs.json` s'arrête actuellement au correctif 0007 alors que 0008–0015 et des patches séparés sont présents. Ne pas exécuter l'ancien script de bundle avant correction de ses références à `PWARASIO.dll` et aux anciens chemins.
2. Importer `client/CONTROL.md`, le client et les tests de protocole/files dans Timox-Pwar. Proposition à valider : placer `client/observe-roundtrip.cjs` et `timox_host_tests.cpp` dans les tests d'intégration TimoxVAudioEngine, car ils consomment moteur/pilote/PWAR. Adapter CMake, SDK/vendor et chemins vers des dépendances explicites, sans copier les binaires de build comme source.
3. Revoir l'intégration déjà présente dans `src/pwar_client_supervisor.cpp`, `src/control_api_server.cpp` et `gui/src/PwarDialog.js`. Corriger et tester le défaut constaté dans `processLine` : rejet des messages sans `version == 1` avant mutation de l'état. Vérifier aussi les erreurs, la fermeture stdin et l'arrêt du processus. Ne pas réimplémenter la supervision depuis zéro.
4. Après autorisation d'exécution, refaire réellement le build Linux avec les headers PipeWire et vérifier la télémétrie par intervalle ; un ancien binaire réutilisé ne valide pas les modifications.
5. Avant tout nouvel essai partagé, obtenir la reprise de l'essai, attribuer la main à un seul opérateur et vérifier processus, ports, liens PipeWire, moteur, carte physique et routes réellement actifs.
6. Analyser les rejets RX Linux et le backlog Windows, corriger la cause démontrée puis exécuter une observation coordonnée. Archiver signal retourné, compteurs et `after-cleanup.json` ; retirer uniquement les routes temporaires de l'essai avec contrôle de révision.
7. Qualifier ensuite traitement DSP Linux, continuité prolongée, dérive, reprise réseau et contrôle distant authentifié. Définir les seuils avant mesure ; conserver `null` pour toute latence/dérive non mesurée.

Sortie extraction : builds reproductibles et contrôle compatible. Sortie qualification distincte : signal aller-retour conforme et scénarios restants démontrés. Les 70 871 frames nulles sur 480 000 et les 116 992 frames rejetées côté Linux interdisent actuellement de qualifier PWAR de stable.

### Lot 5 — Faire évoluer modèle et API

Responsable : parent/API avec sous-agents moteur et GUI. Dépendances : décisions D2 et extraction validée.

1. Extraire les types de domaine et la sérialisation autour de `src/control_api_server.cpp` sans réécriture globale du moteur.
2. Introduire seulement les changements acceptés pour `AudioConfiguration`, `AudioEngine`, les révisions et l'identité des sessions.
3. Mettre à jour ensemble OpenAPI, schémas, documentation FR/EN, clients Electron/GUI, fixtures et tests de contrat. Définir la compatibilité ou la migration de chaque champ modifié.
4. Construire dans TimoxVSSC le mapping `VSSCClient` vers ressources moteur, avec tests des quatre modes de participation et de leur cycle de vie.
5. Vérifier sauvegarde/rechargement, configuration non appliquée, arrêt moteur, conflit de révision et disparition d'une ressource. Une intention persistée ne doit pas afficher une connexion active sans observation.

Sortie : propriété de chaque donnée explicite, ancienne configuration récupérable et état souhaité/appliqué/observé distinguable par le client.

### Lot 6 — Intégrer, vérifier puis retirer les anciennes copies

Responsable : parent/intégration. Dépendances : lots extraits validés ; maturité PWAR annoncée séparément.

1. Construire les artefacts depuis révisions épinglées et consigner chemin, version, commit, timestamp et SHA-256.
2. Adapter les contrôles de packaging : moteur embarqué égal au moteur construit ; DLL dans Setup et ZIP égale au pilote construit. En cas de manque ou de hash différent, arrêter le packaging et reconstruire le composant concerné.
3. Vérifier lancement/découverte du moteur, ressources OpenAPI et licences. Conserver le chemin versionné du pilote et l'installation avec droits administrateur ; le portable n'installe pas le pilote.
4. Valider installation et mise à jour dans un environnement dédié, puis les scénarios audio explicitement autorisés. Préparer le retour aux artefacts précédents et sauvegarder la configuration avant toute migration de format.
5. Retirer les anciennes sources seulement lorsque les nouveaux clones construisent et fonctionnent seuls. Mettre à jour README, AGENTS, chemins canoniques et fichier de reprise dans chaque dépôt.
6. Publication et push constituent une étape distincte à autoriser ; aucun changement de numéro de release automatique.

Sortie : une source canonique par composant, provenance vérifiable et retour arrière documenté.

## 5. Organisation et suivi

Ordre : **0 → 1 → [2, 3, 4] → 5 → 6**. Les imports GUI/PWAR peuvent avancer en parallèle après fixation des contrats ; leurs validations intégrées attendent les artefacts du moteur. Ne pas mélanger extraction, correction audio et refonte API dans un même commit.

| Responsable | Écritures pendant l'exécution | Ne touche pas |
|---|---|---|
| Parent | Décisions, manifeste, contrats transversaux, intégration et packaging final | Essais concurrents avec PWAR |
| Moteur/pilote | TimoxVAudioEngine et fichiers pilote attribués | GUI et transport PWAR |
| GUI | TimoxVSSC | Implémentation du moteur et DLL |
| PWAR | Timox-Pwar et sources PWAR attribuées avant migration | CMake racine, moteur, API, GUI, WASAPI et authentification Timox |

Le parent attribue un propriétaire unique à chaque fichier partagé et centralise les opérations sur le moteur. Chaque lot remet : diff ciblé, provenance, vérifications effectuées, résultats, limites, étape suivante. Utiliser les messages inter-agents pour les demandes et blocages.

Statuts de suivi : à préparer / prêt / en cours / à valider / terminé, avec preuve associée. État initial : planification terminée ; tous les lots d'exécution restent à faire, les décisions D1/D2 restent à valider.

## 6. Limites de cette planification

Aucun code déplacé, build, test, essai audio, accès Linux ou publication effectué. L'état local des clones est vérifié, pas leur accès distant ni leur fonctionnement. Le risque principal est de séparer des contrats aujourd'hui compilés ensemble ou de perdre des changements non commités ; le manifeste, les dépendances épinglées et les validations par lot traitent ces risques.
