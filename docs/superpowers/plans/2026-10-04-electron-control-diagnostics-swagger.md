# Timox VASIO Control : implementation de configuration, Swagger et diagnostic

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer une application Electron qui permet de configurer et contrôler le moteur, de consulter Swagger en local et d’examiner des journaux persistants avec un mode debug.

**Architecture:** Le moteur C++ possède le journal et expose ses diagnostics ainsi que l’arrêt ordonné dans l’API v1 documentée. Electron gère le démarrage du processus et son attachement; le preload expose les opérations nécessaires et React présente Configuration, API et Diagnostic. Le build embarque la spécification OpenAPI et Swagger UI sans ressource externe.

**Tech Stack:** C++20/Win32, cpp-httplib, nlohmann/json, API HTTP/WebSocket v1, Electron 28, React 18, Jest, Node test runner, Electron Builder.

**Spec:** `docs/superpowers/specs/2026-10-04-electron-engine-control-diagnostics-swagger-design.md`

## Contraintes globales

- L’API documentée est le contrat obligatoire pour l’état métier, le diagnostic et les commandes du moteur.
- Le renderer ne lit aucun fichier métier local, registre ou mapping audio.
- L’API reste liée à `127.0.0.1`.
- Les callbacks temps réel ne font ni allocation, ni accès disque, ni journalisation, ni attente bloquante.
- Les échantillons audio et contenus de buffers ne sont jamais écrits dans les journaux.
- Une configuration audio est appliquée comme document complet `configuration.apply`.
- L’arrêt du moteur est refusé tant qu’un client ASIO est attaché.
- Les changements et assets sont limités à TimoxVasio; la publication va au dépôt public `timox/TimoxVasio`.
- Le tag et la publication attendent les vérifications build, API, UI, Swagger et découverte Mixxx.

## Review Focus

- Le callback ne doit jamais appeler le logger, même en mode debug : ajouter une revue et un test d’isolation de l’API logger hors chemin temps réel à la tâche 1.
- Une rotation pendant la lecture du journal ne doit pas exposer de fichier partiel : tester la lecture sous rotation à la tâche 1.
- Deux commandes simultanées de démarrage ne doivent pas créer deux moteurs : couvrir le verrou de démarrage dans les tests du gestionnaire à la tâche 3.
- Une nouvelle connexion client entre vérification et arrêt doit empêcher l’arrêt audio : revérifier l’inventaire dans le point de contrôle natif de la tâche 2.
- Un OpenAPI empaqueté doit correspondre à la version du serveur livré et charger hors ligne : comparer le document embarqué au document source au test de build de la tâche 6.

---

### Task 1: Journal natif persistant et préférences de diagnostic

**Files:**
- Create: `include/engine_diagnostics.h`
- Create: `src/engine_diagnostics.cpp`
- Create: `tests/engine_diagnostics_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/main.cpp`

**Interfaces:**
- `EngineDiagnostics::Initialize(std::wstring directory)` ouvre `%LOCALAPPDATA%\TimoxVasio\logs\engine.log` et charge `diagnostics.json`.
- `EngineDiagnostics::Write(Level, std::string_view component, std::string_view message)` écrit une entrée horodatée au niveau actif.
- `EngineDiagnostics::SetLevel(Level)` persiste `info` ou `debug` et met à jour le niveau actif.
- `EngineDiagnostics::ReadRecent(std::size_t limit)` retourne au plus 500 entrées complètes, de la plus ancienne à la plus récente dans la fenêtre demandée.

- [ ] **Step 1: Écrire les tests rouges du logger**

Créer les cas suivants avec un répertoire temporaire isolé : `info` omet les entrées `debug`; `debug` les conserve; lignes au schéma timestamp/level/component/message; rejet des valeurs de niveau invalides; rotation à 5 Mio avec quatre archives; lecture bornée de 1 à 500 entrées; lecture cohérente pendant une rotation; préférence de niveau restaurée après recréation du logger.

- [ ] **Step 2: Exécuter les tests logger et vérifier l’échec attendu**

Run: `cmake --build build_api --target EngineDiagnosticsTests --config Release`
Run: `build_api\EngineDiagnosticsTests.exe`
Expected: la cible ou les assertions échouent parce que les types/logger n’existent pas encore.

- [ ] **Step 3: Implémenter le logger hors des callbacks audio**

Créer le répertoire avec les API Win32, sérialiser chaque ligne en JSON UTF-8, protéger l’écriture par un verrou interne, appliquer l’archivage à 5 Mio, conserver quatre archives et lire une fenêtre cohérente de lignes. Aucun objet logger n’est appelé depuis `AudioRoutingRuntime`, `VASIODriver::bufferSwitch` ou le callback du pilote physique.

- [ ] **Step 4: Initialiser et alimenter les événements de cycle de vie**

Initialiser le logger dans `wmain` avant le démarrage des clients et du contrôleur. Écrire les événements démarrage, échec de démarrage, `api.ready`, ouverture/fermeture API, erreur contrôleur et arrêt ordonné. Garder stdout initial compatible avec l’événement `api.ready` existant.

- [ ] **Step 5: Exécuter les tests logger et vérifier les critères limites**

Run: `cmake --build build_api --target EngineDiagnosticsTests --config Release`
Run: `build_api\EngineDiagnosticsTests.exe`
Expected: tous les cas précédents passent; vérifier que le fichier courant et quatre archives au plus existent sous le répertoire temporaire.

### Task 2: Routes API diagnostics et arrêt ordonné

**Files:**
- Modify: `include/control_api_server.h`
- Modify: `src/control_api_server.cpp`
- Modify: `include/audio_controller.h`
- Modify: `src/audio_controller.cpp`
- Modify: `schemas/api-v1.json`
- Modify: `openapi-v1.json`
- Create: `tests/engine_diagnostics_api_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `GET /api/v1/diagnostics?limit=N` retourne `{level, entries}`; `N` par défaut 200 et valide de 1 à 500.
- `PUT /api/v1/diagnostics` reçoit exactement `{level: "info"|"debug"}` et retourne le niveau confirmé.
- Le message WebSocket `{id, command:"engine.stop", payload:{}}` retourne une erreur `ENGINE_CLIENTS_CONNECTED` si le snapshot contient un client; sinon il acquitte puis signale l’arrêt au thread principal.

- [ ] **Step 1: Écrire les tests rouges des routes de diagnostic et d’arrêt**

Utiliser un contrôleur et un logger avec répertoire temporaire. Tester lecture par défaut, limites 1 et 500, rejet de 0/501, niveau info/debug, champ inconnu, niveau invalide, arrêt refusé quand un client est connecté, arrêt accepté sans client, nouvel attachement refusé après réservation d’arrêt, événement final `engine.status`.

- [ ] **Step 2: Exécuter les tests API et confirmer leur échec fonctionnel**

Run: `cmake --build build_api --target EngineDiagnosticsApiTests --config Release`
Run: `build_api\EngineDiagnosticsApiTests.exe`
Expected: compilation ou assertions échouent parce que les routes et commande n’existent pas.

- [ ] **Step 3: Ajouter les schémas de diagnostic et l’API HTTP**

Valider `limit` avant lecture; sérialiser timestamps et niveaux; refuser les propriétés inconnues du PUT; retourner une erreur structurée `INVALID_DIAGNOSTICS_CONFIGURATION` pour un corps invalide. Ne jamais retourner un contenu audio.

- [ ] **Step 4: Ajouter la commande WebSocket d’arrêt protégé**

Réserver l’arrêt de façon atomique avec la découverte des clients : figer toute nouvelle publication/attache, vérifier qu’il n’existe aucun client, puis acquitter et déclencher l’événement Win32 consommé par `wmain`. S’il existe un client, libérer la réservation et répondre sans mutation. Fermer l’API, le contrôleur et le gestionnaire dans l’ordre de teardown existant.

- [ ] **Step 5: Mettre OpenAPI et le schéma partagé à jour**

Déclarer GET/PUT diagnostics, paramètres, réponses, erreurs, modèle des entrées, commande WebSocket `engine.stop` et erreur client-connecté. Conserver les routes et noms historiques exclus par le contrat courant.

- [ ] **Step 6: Exécuter les tests API et vérifier le contrat**

Run: `cmake --build build_api --target EngineDiagnosticsApiTests --config Release`
Run: `build_api\EngineDiagnosticsApiTests.exe`
Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests\api-contract.ps1`
Expected: tests API verts et vérificateur OpenAPI accepte chaque nouvelle route et commande.

### Task 3: Gestion sûre du processus moteur dans Electron

**Files:**
- Create: `gui/electron/engine-manager.js`
- Create: `gui/electron/engine-manager.test.js`
- Modify: `gui/electron/main.js`
- Modify: `gui/electron/preload.js`

**Interfaces:**
- `EngineManager.start()` attache une API TimoxVasio déjà active ou démarre un seul exécutable.
- `EngineManager.stop()` lit les clients par API, refuse si un client existe, demande `engine.stop`, attend la fermeture et confirme l’absence d’API.
- `EngineManager.restart()` exécute stop puis start, sans période où deux processus identifiés TimoxVasio sont actifs.
- IPC allowlist : `engine:get-lifecycle`, `engine:start`, `engine:stop`, `engine:restart`.

- [ ] **Step 1: Écrire les tests rouges du gestionnaire de processus**

Injecter spawn, fetch et ApiClient. Tester attachement sans spawn, un seul spawn pour deux start simultanés, arrêt refusé avec clients, arrêt commandé puis attente API indisponible, timeout conservant l’état `error`, restart ordonné, fermeture de fenêtre qui détache un moteur avec clients.

- [ ] **Step 2: Exécuter le test de gestionnaire et vérifier l’échec attendu**

Run: `node --test gui/electron/engine-manager.test.js`
Expected: échec d’import avant création du gestionnaire.

- [ ] **Step 3: Extraire la propriété du processus dans EngineManager**

Déplacer le chemin dev/packagé, la détection du moteur 52525, les événements de processus et le verrou de démarrage dans le nouveau composant injectable. Valider l’identité API avant de s’attacher à un moteur existant.

- [ ] **Step 4: Mettre en œuvre arrêt, redémarrage et fermeture sans perte client**

Utiliser uniquement la commande `engine.stop`; ne pas appeler `child.kill()` comme arrêt nominal. Ne pas arrêter un moteur attaché ou possédé qui a un client. Quand l’application ferme sans client, demander l’arrêt ordonné; avec client, détacher le processus et laisser le moteur tourner.

- [ ] **Step 5: Exposer des IPC étroits au preload**

Exposer état du cycle de vie et commandes start/stop/restart sans accès arbitraire au système de fichiers ni au shell. Le renderer ne transmet aucun chemin de processus.

- [ ] **Step 6: Exécuter les tests gestionnaire**

Run: `node --test gui/electron/engine-manager.test.js`
Expected: couverture verte de toutes les branches d’attachement, concurrence, clients actifs et timeout.

### Task 4: Client Electron, Swagger local et vues Diagnostic

**Files:**
- Modify: `gui/electron/api-client.js`
- Modify: `gui/electron/api-client.test.js`
- Modify: `gui/electron/main.js`
- Modify: `gui/electron/preload.js`
- Modify: `gui/src/App.js`
- Create: `gui/src/ApiDocs.js`
- Create: `gui/src/ApiDocs.test.js`
- Modify: `gui/src/App.test.js`
- Modify: `gui/package.json`
- Modify: `gui/package-lock.json`

**Interfaces:**
- `ApiClient.getDiagnostics(limit)` et `ApiClient.setDiagnosticLevel(level)` appellent les seules routes documentées.
- Le renderer utilise `window.vasio.getDiagnostics`, `setDiagnosticLevel`, `stopEngine`, `startEngine`, `restartEngine` et `openLogDirectory`.
- La vue API charge l’objet OpenAPI local inclus au build et affiche la documentation WebSocket locale.

- [ ] **Step 1: Écrire tests rouges ApiClient et navigation UI**

Tester GET diagnostics avec limite, PUT `info`/`debug`, propagation d’erreur API; tester bascule des onglets, blocage de Stop avec clients, rendu des entrées de journal, bascule du niveau; tester la vue API sur une spécification locale et le titre `configuration.apply`.

- [ ] **Step 2: Exécuter les tests pour confirmer l’absence des fonctions et vues**

Run: `cd gui; node --test electron/api-client.test.js`
Run: `cd gui; npm run react-test -- --watchAll=false --runInBand`
Expected: nouveaux cas en échec car méthodes et vues ne sont pas présentes.

- [ ] **Step 3: Implémenter les méthodes ApiClient et IPC du diagnostic**

Ajouter GET/PUT au client, vérifier que seuls les niveaux `info` et `debug` passent vers l’API, mapper les réponses et erreurs structurées; exposer les opérations par les handlers Electron puis par le preload.

- [ ] **Step 4: Ajouter la navigation Configuration / API / Diagnostic**

Déplacer les sections existantes sous une vue Configuration sans changer leurs sources API. Ajouter les boutons de cycle de vie; les désactiver si l’API est absente, un changement est en cours ou des clients sont présents pour stop/restart.

- [ ] **Step 5: Embarquer Swagger UI et OpenAPI**

Ajouter `swagger-ui-react` comme dépendance locale. Importer `../openapi-v1.json` au renderer à la compilation; configurer Swagger UI sans `url` distant, sans CDN, sans requête sortante, et sans fonctionnalité d’essai qui contournerait l’API locale. Afficher les commandes/events WebSocket depuis un composant texte structuré alimenté par le même contrat.

- [ ] **Step 6: Ajouter la vue Diagnostic**

Afficher moteur, PID/attachement, clients, niveau confirmé, dernières entrées du journal, erreur structurée et boutons actualiser/exporter/ouvrir dossier. Le bouton debug appelle `setDiagnosticLevel` et rend l’état seulement après réponse API.

- [ ] **Step 7: Exécuter les tests ApiClient et UI**

Run: `cd gui; node --test electron/api-client.test.js`
Run: `cd gui; npm run contract-test`
Run: `cd gui; npm run react-test -- --watchAll=false --runInBand`
Expected: tests nouveaux et existants verts; aucun test ne lit le registre, le fichier profils ou le mapping directement.

### Task 5: Corriger la lisibilité de l’interface

**Files:**
- Modify: `gui/src/index.css`
- Modify: `gui/src/App.css`
- Modify: `gui/src/App.test.js`

**Interfaces:**
- Une échelle CSS partagée fournit couleur, typographie, surfaces, rayon, espacement et focus.
- Les trois vues utilisent les mêmes composants de navigation, encart et message de statut.

- [ ] **Step 1: Ajouter assertions de structure/accessibilité visuelle**

Tester rôle et état sélectionné des onglets, focusable au clavier, libellés textuels pour statut, et classes sémantiques de surface plutôt que règles de couleurs dispersées.

- [ ] **Step 2: Vérifier leur échec avant styles**

Run: `cd gui; npm run react-test -- --watchAll=false --runInBand`
Expected: états navigation/focus accessibles non rendus.

- [ ] **Step 3: Définir les tokens et appliquer les corrections**

Définir une seule pile de police et taille 16 px; augmenter `.hint`, résumés et matrice conformément à la spec; limiter cyan aux accents; différencier les fonds section/carte; uniformiser bordures à 1 px neutre, rayons 6 px, hauteur de contrôle à 40 px et focus visible contrasté. Retirer les styles globaux en conflit et les règles mortes identifiées.

- [ ] **Step 4: Exécuter React tests et build de production**

Run: `cd gui; npm run react-test -- --watchAll=false --runInBand`
Run: `cd gui; npm run react-build`
Expected: tous les tests passent et le build ne signale ni erreur ni avertissement CSS bloquant.

### Task 6: Packager, valider visuellement et publier

**Files:**
- Modify: `gui/package.json`
- Modify: `gui/GUI_GUIDE.md`
- Modify: `gui/README.md`
- Modify: `INSTALL.md`
- Modify: `API.md`
- Modify: `openapi-v1.json`
- Modify: `schemas/api-v1.json`
- Modify: `docs/superpowers/plans/IMPLEMENTATION_LEDGER.md`

- [ ] **Step 1: Compiler le moteur et exécuter les suites ciblées**

Run: `cmake --build build_api --target TimoxVirtualAsioEngine EngineDiagnosticsTests EngineDiagnosticsApiTests --config Release`
Run: `build_api\EngineDiagnosticsTests.exe`
Run: `build_api\EngineDiagnosticsApiTests.exe`
Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tests\api-contract.ps1`
Expected: build x64 réussit, tests diagnostics et contrat API passent.

- [ ] **Step 2: Construire les artefacts Electron**

Run: `cd gui; npm run contract-test`
Run: `cd gui; npm run react-test -- --watchAll=false --runInBand`
Run: `cd gui; npm run electron-build`
Expected: build React réussi, installateur et exécutable portable produits; le backend, `openapi-v1.json` et Swagger UI sont empaquetés localement.

- [ ] **Step 3: Vérifier le contenu du package**

Inspecter `gui/dist/win-unpacked/resources/app.asar` et `resources/backend/TimoxVirtualAsioEngine.exe`; confirmer l’absence d’URL CDN et la présence du document OpenAPI. Ouvrir l’app empaquetée, consulter les trois vues et vérifier la console réseau sans requête externe.

- [ ] **Step 4: Inspecter une capture de chaque vue**

Capturer les vues Configuration, API et Diagnostic du package actuel, inspecter les fichiers capturés et vérifier taille des textes, hiérarchie des surfaces, focus clavier et absence de sections coupées.

- [ ] **Step 5: Vérifier la découverte Mixxx et l’état audio publiable**

Confirmer dans Mixxx le pilote TimoxVasio et dans l’API les canaux ouverts. Valider un signal uniquement après choix/apport du routage SSL 12; si aucun signal physique n’est mesuré, publier en indiquant explicitement que le pilote/API sont vérifiés et que le chemin matériel ne l’est pas.

- [ ] **Step 6: Préparer les documents de livraison**

Mettre à jour guides et ledger avec les commandes de diagnostic, chemin des logs, récupération Swagger, version de build, profils et critères audio réellement vérifiés. Ne pas écrire `achieved` pour un chemin audio sans mesure.

- [ ] **Step 7: Préparer le tag et la publication sur TimoxVasio**

Vérifier d’abord le remote et la branche du clone public TimoxVasio; vérifier que le commit ne contient que les changements de ce projet. Créer un tag versionné, joindre l’installateur et le portable.

## Auto-revue du plan

- Couverture : le logger natif, le contrat API, le cycle de vie Electron, Swagger, l’UI diagnostic, les corrections UX, le build et la livraison sont couverts.
- Contrat : toutes les données métier restent API; seules les opérations de démarrage OS passent par IPC étroit.
- Temps réel : le plan interdit explicitement log, allocation, disque et attente dans tous les callbacks audio.
- Limites : lecture logs bornée 1–500, rollover défini, démarrage concurrent, arrêt avec client, Swagger hors-ligne et remote Git ciblé ont des contrôles dédiés.
- État audio : le routage SSL reste une validation conditionnelle distincte de la publication de l’outil; aucun faux succès n’est déclaré.
