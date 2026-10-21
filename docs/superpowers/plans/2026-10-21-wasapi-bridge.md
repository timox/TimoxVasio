# Plan d'implémentation du pont Windows Audio (WASAPI)

> **Pour les agents codeurs :** ce fichier est l'instruction complète. Exécuter les tâches dans l'ordre, une par une, avec les tests écrits avant le code (`- [ ]` à cocher au fur et à mesure). Ne pas commencer une tâche tant que la précédente n'est pas validée et commitée. Lire d'abord `AGENTS.md`, puis la spécification.

**Objectif :** livrer TimoxVasio 1.3 avec des ponts WASAPI qui relient les applications ASIO à l'audio Windows (phase 1), puis un mode sans carte ASIO où un périphérique WASAPI exclusif fournit l'horloge (phase 2).

**Architecture :** chaque pont possède un thread MMCSS, un `IAudioClient` et un anneau SPSC par direction. Le callback ASIO physique lit et écrit ces anneaux sans jamais bloquer ; la conversion de format et la correction de dérive vivent dans le thread du pont. Le graphe de routage gagne deux types d'extrémités `WasapiInput` et `WasapiOutput`. L'API `configuration.apply` et `GET /api/v1/state` restent les seuls chemins de configuration et d'état.

**Pile :** Windows C++20, Core Audio (`mmdeviceapi.h`, `audioclient.h`, `avrt.lib`), ASIO SDK, JSON Schema / OpenAPI 3.1, React / Electron, CMake, PowerShell.

**Spécification normative :** `docs/WASAPI_BRIDGE_SPEC.md` (anglais : `docs/WASAPI_BRIDGE_SPEC.en.md`). En cas de doute, la spécification prime sur ce plan ; si le plan et la spécification se contredisent, corriger les deux dans le même commit et le signaler dans le message.

## Contraintes globales

Ces règles s'appliquent à chaque tâche. Une PR qui en viole une est refusée.

1. **Règles du dépôt (`AGENTS.md`).** Travailler à la racine renvoyée par `git rev-parse --show-toplevel`. Vérifier `git status` avant tout build. Ne jamais choisir un binaire par son nom seul : chemin, horodatage et SHA-256. Ne pas empaqueter si un hash diffère.
2. **Règle d'interface, sans exception.** La navigation gauche contient des sections et des états ou actions globaux uniquement. Les actions sur les objets de routage partent du menu contextuel du fond, d'un nœud ou d'un câble ; leurs formulaires sont des modales. Aucun panneau permanent de propriétés à côté du graphe. Sélectionner un câble n'ouvre rien.
3. **Callback audio.** Aucune allocation, aucun verrou, aucune attente, aucun journal, aucun appel COM ou WASAPI dans `AudioRoutingRuntime::Process`, dans `RoutingGraph::Process` ni dans le callback de `PhysicalAsioHost`. Les seules opérations ajoutées sont `WasapiBridge::ReadBlock` et `WriteBlock`, qui ne font que copier depuis ou vers un anneau préalloué.
4. **Chemin ASIO sans conversion.** Aucun rééchantillonnage entre les clients TimoxVasio et la carte physique. Toute conversion se fait dans le thread du pont.
5. **Rien hors API.** Le renderer Electron et `electron/main.js` ne touchent ni WASAPI, ni le registre, ni la mémoire partagée. Tout passe par `GET /api/v1/state`, `GET /api/v1/drivers` et les commandes WebSocket documentées.
6. **Compatibilité ascendante.** Un client qui omet `wasapiBridges` conserve les ponts enregistrés. Un `audio-configuration.json` antérieur se charge sans pont. Les identifiants d'endpoints existants ne changent pas. `apiVersion` passe à `1.1` dans la tâche 1 et nulle part ailleurs.
7. **Composants testables hors Windows.** `src/drift_resampler.*`, `src/spsc_audio_ring.h` et la validation de configuration des ponts n'incluent aucun en-tête Windows, pour être testés par CTest sans périphérique audio. Les composants WASAPI (`wasapi_bridge.cpp`, `wasapi_device_enumerator.cpp`, `wasapi_exclusive_host.cpp`) sont ajoutés dans le bloc `if(WIN32)` de `CMakeLists.txt`.
8. **Documentation bilingue.** Chaque fichier `docs/*.md` modifié a son pendant `.en.md` mis à jour dans le même commit. Les messages de commit sont en français, à l'impératif, comme l'historique du dépôt.
9. **Pas de dépendance tierce nouvelle** sans décision explicite du mainteneur. Le rééchantillonneur est écrit dans le dépôt. `libsamplerate` n'est introduit que si le test de qualité de la tâche 2 échoue, et dans ce cas avec une entrée dans `docs/LICENSING.md`.
10. **Pas de livrable binaire dans ce plan.** La version 1.3 (numéros, release notes, Setup, ZIP, empreintes) est une tâche séparée demandée par le mainteneur après validation des phases.
11. **Processus.** Avant d'arrêter un processus de test, identifier PID, commande complète, appartenance au projet et session. Ne jamais arrêter un moteur ou un hôte ASIO d'identité inconnue.

## Environnement et commandes

Build moteur (Windows, Visual Studio 2026) :

```powershell
cmake -S . -B build_codex_110 -G "Visual Studio 18 2026" -A x64
cmake --build build_codex_110 --config Release --target TimoxVirtualAsioEngine
```

Tests natifs : chaque exécutable de test de `CMakeLists.txt` est construit avec `--target <Nom>` puis lancé depuis `build_codex_110\Release\`. Les nouveaux tests suivent le même modèle que `RoutingGraphTests` et `AudioRoutingRuntimeTests` (exécutable autonome, code de retour non nul en cas d'échec, pas de framework).

Contrat API et interface (voir `docs/TEST_VERIFICATION.md`) :

```powershell
pwsh -NoProfile -File .\tests\api-contract.ps1
cd gui
npm run contract-test
$env:CI='true'
npm run react-test -- --watchAll=false --runInBand
node --check electron/main.js
```

Sur un poste sans Windows, les tâches 1, 2, 3 et 10 sont réalisables et vérifiables en entier (JSON, logique pure, React). Les tâches 4 à 9 et 11 à 13 compilent uniquement sous Windows ; un agent sans Windows les écrit, s'assure qu'elles sont cohérentes avec les en-têtes existants, et signale explicitement dans le message de commit que le build n'a pas été exécuté.

## Carte des fichiers

| Fichier | Rôle dans ce plan |
|---|---|
| `api/schemas/api-v1.json`, `api/openapi-v1.json`, `docs/API.md`, `docs/API.en.md`, `tests/api-contract.ps1`, `gui/src/api-contract.test.cjs` | Contrat normatif : ponts, inventaire, événement, erreurs |
| `src/drift_resampler.h`, `src/drift_resampler.cpp`, `tests/drift_resampler_tests.cpp` | Rééchantillonneur à ratio variable et régulateur PI, sans Windows |
| `src/spsc_audio_ring.h`, `tests/spsc_audio_ring_tests.cpp` | Anneau SPSC float32 entrelacé |
| `include/wasapi_bridge_configuration.h`, `src/wasapi_bridge_configuration.cpp`, `tests/wasapi_bridge_configuration_tests.cpp` | Structures de configuration, validation, (dé)sérialisation JSON |
| `include/routing_graph.h`, `src/routing_graph.cpp`, `tests/routing_graph_tests.cpp` | Types `WasapiInput`, `WasapiOutput` et table des liaisons |
| `include/wasapi_device_enumerator.h`, `src/wasapi_device_enumerator.cpp` | Inventaire MMDevice, suivi des défauts, notifications |
| `include/wasapi_bridge.h`, `src/wasapi_bridge.cpp`, `tests/wasapi_bridge_probe.cpp` | Un pont : activation, négociation, thread, anneaux, santé |
| `include/audio_routing_runtime.h`, `src/audio_routing_runtime.cpp` | Appel des ponts autour du graphe |
| `include/audio_controller.h`, `src/audio_controller.cpp`, `src/audio_configuration_store.cpp`, `src/persistent_routes.h` | Cycle de configuration, persistance, routes en attente |
| `src/control_api_server.cpp` | Inventaire, validation, événement `wasapi.bridge.status` |
| `gui/src/configuration-merge.js`, `gui/src/RoutingPatchbay.js`, `gui/src/App.js`, `gui/src/ProfessionalTheme.css` et leurs tests | Section `Audio setup`, nœuds de pont, modales, carte Performance |
| `include/clock_host.h`, `include/wasapi_exclusive_host.h`, `src/wasapi_exclusive_host.cpp` | Phase 2 : source de cadence abstraite et hôte exclusif |
| `tools/vasio-doctor/` | Contrôles loopback par processus et mode exclusif |
| `docs/ARCHITECTURE.md`, `docs/STRUCTURE.md`, `docs/FONCTIONS.md`, `README.md` et leurs `.en.md` | Documentation finale |

## Points de revue

Les relecteurs vérifient en priorité ces risques ; chaque tâche indique le test qui le couvre.

- Un underrun d'anneau ne doit jamais bloquer ni ralentir le callback ASIO : la tâche 3 le fixe par test, la tâche 6 par mesure de `physical.routing` avant et après.
- Le régulateur ne doit pas osciller ni dépasser ±0,2 % : tâche 2.
- Un périphérique absent au démarrage ne doit pas empêcher `running` si une autre route est complète : tâche 7.
- `configuration.apply` sans `wasapiBridges` ne doit pas effacer les ponts ; `[]` doit les effacer : tâches 1 et 7.
- Le loopback sans flux de rendu actif doit continuer à produire des blocs : tâche 5.
- Perte de périphérique en rôle `slave` : le flux ASIO continue ; en rôle `master` : `engine.error` `WASAPI_DEVICE_LOST` : tâches 7 et 12.
- Aucune fonction de l'interface ne contourne la règle 2 : tâche 10, revue manuelle des captures.

---

## Phase 1 : ponts esclaves

### Tâche 1 : publier le contrat API

**Fichiers :** modifier `api/schemas/api-v1.json`, `api/openapi-v1.json`, `docs/API.md`, `docs/API.en.md`, `tests/api-contract.ps1`, `gui/src/api-contract.test.cjs`.

**Interfaces :** ajouter les définitions `wasapiDevice`, `wasapiDeviceSelector`, `wasapiProcessFilter`, `wasapiBridge`, `wasapiBridgeStatus`, `wasapiBridgeStatusEvent` ; étendre `state` et `drivers` avec `wasapiDevices` et `wasapiBridges` (requis), `applyCommand` avec `wasapiBridges` (optionnel), `event` avec `wasapi.bridge.status`, `error.code` avec les huit codes de la spécification §8.5 et `error.hresult`. `apiVersion` devient `"1.1"`. Les exemples JSON de la spécification §8.1 et §8.2 sont les fixtures.

- [ ] Ajouter aux tests de contrat des fixtures valides (§8.1, §8.2, phase 2 sans carte) et invalides : `id` de pont hors `[A-Za-z0-9_-]{1,64}`, `kind: "loopback"` avec `flow: "capture"`, `clockRole: "master"` avec `shareMode: "shared"`, `channels: 0`, `targetLatencyMs: 501`, statut sans `state`. Les tests doivent échouer avant modification des schémas.
- [ ] Lancer `pwsh -NoProfile -File .\tests\api-contract.ps1` et `npm run contract-test` dans `gui` ; constater l'échec.
- [ ] Écrire les définitions dans le JSON Schema et l'OpenAPI (routes HTTP et `x-websocket`), puis la section « Ponts Windows Audio (WASAPI) » de `API.md` et `API.en.md` : champs, sélecteurs, règles de validation ordonnées (§8.2), règle de conservation (§8.3), événement, codes d'erreur. Ajouter `wasapiBridges`, `wasapi.bridge.status` et `wasapi:` à la liste `$required` de `tests/api-contract.ps1`.
- [ ] Relancer les deux commandes ; exiger le succès. Commit : `Publier le contrat API des ponts WASAPI`.

### Tâche 2 : rééchantillonneur à ratio variable et régulateur

**Fichiers :** créer `src/drift_resampler.h`, `src/drift_resampler.cpp`, `tests/drift_resampler_tests.cpp` ; modifier `CMakeLists.txt` (cible `DriftResamplerTests`, hors bloc `WIN32`).

**Interfaces :**

```cpp
class DriftResampler final {
public:
    DriftResampler(std::uint32_t channels, double nominalRatio, std::uint32_t taps = 32);
    void SetRatio(double ratio) noexcept;           // borné à nominal × (1 ± 0,002)
    double Ratio() const noexcept;
    // Consomme jusqu'à inputFrames, produit jusqu'à outputCapacity. Jamais d'allocation après le constructeur.
    struct Progress { std::uint32_t consumed, produced; };
    Progress Process(const float* interleavedIn, std::uint32_t inputFrames,
                     float* interleavedOut, std::uint32_t outputCapacity) noexcept;
    void Reset() noexcept;
};

class FillLevelController final {
public:
    FillLevelController(double targetFrames, double kp, double ki, double maxDeviation = 0.002);
    double Update(double measuredFillFrames, double elapsedSeconds) noexcept; // renvoie le ratio relatif
    void Reset() noexcept;
};
```

- [ ] Écrire les tests : ratio 1 sur une sinusoïde 1 kHz à 48 kHz, SNR > 90 dB ; ratio 44 100/48 000 et inverse, SNR > 90 dB à 1 kHz et à 10 kHz ; conservation du nombre d'échantillons sur 60 s simulées à ±1 frame ; `SetRatio` hors bande est borné ; `Process` avec `outputCapacity` trop petit ne perd aucun échantillon d'entrée. Régulateur : pas de +100 ppm, retour à ±5 ppm en moins de 10 s simulées, aucun dépassement de la bande, pas d'oscillation entretenue (amplitude décroissante sur trois périodes).
- [ ] Construire et lancer `DriftResamplerTests` ; constater l'échec.
- [ ] Implémenter : polyphase à fenêtre de sinc (Kaiser ou Blackman-Harris), table de phases précalculée au constructeur, interpolation linéaire entre phases, état d'historique par canal, arithmétique en double pour la position fractionnaire. Régulateur PI avec anti-emballement.
- [ ] Relancer les tests ; exiger le succès. Si le SNR cible n'est pas atteint après ajustement des taps, documenter la mesure dans le commit et ouvrir la question `libsamplerate` au mainteneur sans l'introduire. Commit : `Ajouter le rééchantillonneur à ratio variable du pont WASAPI`.

### Tâche 3 : anneau SPSC audio

**Fichiers :** créer `src/spsc_audio_ring.h`, `tests/spsc_audio_ring_tests.cpp` ; modifier `CMakeLists.txt` (cible `SpscAudioRingTests`, hors bloc `WIN32`).

**Interfaces :**

```cpp
class SpscAudioRing final {
public:
    SpscAudioRing(std::uint32_t channels, std::uint32_t capacityFrames);
    std::uint32_t Write(const float* interleaved, std::uint32_t frames) noexcept;  // renvoie les frames écrites
    std::uint32_t Read(float* interleaved, std::uint32_t frames) noexcept;         // renvoie les frames lues, le reste est mis à zéro par l'appelant
    std::uint32_t ReadPlanar(float* const* planar, std::uint32_t frames) noexcept;
    std::uint32_t WritePlanar(const float* const* planar, std::uint32_t frames) noexcept;
    std::uint32_t Available() const noexcept;
    std::uint32_t Capacity() const noexcept;
    void Clear() noexcept;
    void Prefill(std::uint32_t silentFrames) noexcept;
    std::uint64_t Underruns() const noexcept;
    std::uint64_t Overruns() const noexcept;
};
```

- [ ] Écrire les tests : lecture et écriture concurrentes depuis deux threads sur 10 s avec motif vérifiable, aucune perte hors underrun/overrun comptés ; lecture au-delà du disponible renvoie le nombre réel et incrémente `Underruns` ; écriture au-delà de la place renvoie le nombre réel et incrémente `Overruns` ; `Prefill` puis `Available` ; aucune allocation après le constructeur (vérifier par un compteur d'allocations global dans le test).
- [ ] Construire et lancer ; constater l'échec, implémenter avec `std::atomic<std::uint64_t>` et ordres `acquire`/`release`, relancer. Commit : `Ajouter l'anneau SPSC des ponts WASAPI`.

### Tâche 4 : configuration des ponts et règles de liaison

**Fichiers :** créer `include/wasapi_bridge_configuration.h`, `src/wasapi_bridge_configuration.cpp`, `tests/wasapi_bridge_configuration_tests.cpp` ; modifier `include/routing_graph.h`, `src/routing_graph.cpp`, `tests/routing_graph_tests.cpp`, `include/audio_controller.h` (champ `std::vector<WasapiBridgeConfiguration> wasapiBridges` dans `AudioControllerConfiguration`), `src/audio_configuration_store.cpp`, `tests/audio_configuration_store_tests.cpp`, `CMakeLists.txt`.

**Interfaces :** structures `WasapiDeviceSelector`, `WasapiProcessFilter`, `WasapiBridgeConfiguration` de la spécification §7.2 ; fonctions `ParseWasapiBridges(const nlohmann::json&, std::vector<WasapiBridgeConfiguration>&, std::string& error, std::string& errorCode)`, `SerializeWasapiBridges(...)`, `ValidateWasapiBridges(const std::vector<WasapiBridgeConfiguration>&, bool hasPhysicalDriver, bool processLoopbackSupported, std::string& error, std::string& errorCode)` ; `RoutingEndpointType::WasapiInput` et `WasapiOutput` ; `ParseWasapiEndpointId(const std::string&, std::string& bridgeId, bool& isInput, std::uint32_t& channel)`.

- [ ] Tests de configuration : chaque ligne du tableau de validation §8.2 produit le code attendu ; aller-retour JSON exact ; `audio-configuration.json` sans `wasapiBridges` se charge avec un vecteur vide ; un fichier avec ponts se recharge à l'identique.
- [ ] Tests de graphe : étendre `routing_graph_tests.cpp` à la table §5.4 (neuf combinaisons), et vérifier qu'un graphe avec endpoints WASAPI traite correctement gain et mute sur les trois nouvelles liaisons.
- [ ] Construire, constater l'échec, implémenter, relancer `WasapiBridgeConfigurationTests`, `RoutingGraphTests`, `AudioConfigurationStoreTests`. Commit : `Décrire et valider la configuration des ponts WASAPI`.

### Tâche 5 : énumérateur et pont WASAPI

**Fichiers :** créer `include/wasapi_device_enumerator.h`, `src/wasapi_device_enumerator.cpp`, `include/wasapi_bridge.h`, `src/wasapi_bridge.cpp`, `tests/wasapi_bridge_probe.cpp` ; modifier `CMakeLists.txt` (sources moteur, cible `WasapiBridgeProbe`, lien `avrt`, bloc `WIN32`).

**Interfaces :** `WasapiDeviceEnumerator` avec `Enumerate() -> std::vector<WasapiDeviceInfo>` (id, nom, flux, état, rôles par défaut, format de mixage, `minSharedPeriodFrames`, `exclusiveSampleRates`), `Resolve(const WasapiDeviceSelector&) -> std::optional<std::wstring>`, `SetListener(std::function<void()>)` pour les notifications. `WasapiBridge` conforme à la spécification §7.2.

Règles d'implémentation :

- Thread du pont : `CoInitializeEx(COINIT_MULTITHREADED)`, `AvSetMmThreadCharacteristicsW(L"Pro Audio")`, `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
- Mode partagé : tenter le format du graphe en float32 avec `AUTOCONVERTPCM | SRC_DEFAULT_QUALITY` ; sur `AUDCLNT_E_UNSUPPORTED_FORMAT`, ouvrir le format de mixage et laisser `DriftResampler` au ratio nominal `mix/graph`. Utiliser `IAudioClient3::InitializeSharedAudioStream` avec la période minimale quand l'interface existe, sinon `IAudioClient::Initialize`.
- Loopback : flux de rendu silencieux parallèle sur le même périphérique pour obtenir les événements (§6.4) ; repli sur minuterie à demi-période si l'ouverture du flux silencieux échoue.
- Loopback par processus : `ActivateAudioInterfaceAsync` avec `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` ; résolution de `processName` par `CreateToolhelp32Snapshot`, instance la plus ancienne, ambiguïté dans `lastError` sans échec.
- Latence par défaut et capacité selon §6.3. Préremplir l'anneau à la consigne au démarrage.
- Mesure : `measuredLatencyMs` = remplissage moyen de l'anneau sur 1 s + période WASAPI ; `driftPpm` = (ratio appliqué / ratio nominal − 1) × 10⁶.
- Perte : sur `AUDCLNT_E_DEVICE_INVALIDATED` ou notification MMDevice, état `DeviceLost`, anneau vidé, réouverture avec recul 250 ms, 500 ms, 1 s, 2 s, 5 s puis 5 s.
- `ReadBlock` et `WriteBlock` n'appellent que `SpscAudioRing::ReadPlanar` / `WritePlanar` et complètent par des zéros.

- [ ] Écrire `WasapiBridgeProbe` : options `--kind capture|loopback|render`, `--device default|<id>`, `--exclusive`, `--process <exe>`, `--seconds 30`, `--graph-rate 48000 --graph-frames 256` ; simule le callback ASIO par une minuterie haute résolution et imprime la santé chaque seconde ; code de retour non nul si `underruns + overruns > 0` après les cinq premières secondes ou si `resyncs > 0`.
- [ ] Implémenter l'énumérateur puis le pont. Construire `TimoxVirtualAsioEngine` et `WasapiBridgeProbe`.
- [ ] Sur une machine Windows, lancer la sonde pour les trois types sur le périphérique par défaut, puis `--process` sur un navigateur en lecture, puis débrancher ou désactiver le périphérique pendant la sonde et vérifier la reprise. Consigner les résultats dans le message de commit. Commit : `Ajouter l'énumérateur et le pont WASAPI`.

### Tâche 6 : intégration dans le runtime de routage

**Fichiers :** modifier `include/audio_routing_runtime.h`, `src/audio_routing_runtime.cpp`, `tests/audio_routing_runtime_tests.cpp`, `tests/performance_stage_measurements_tests.cpp`.

**Interfaces :** `AudioRoutingLayout` gagne `std::vector<WasapiBridge*> bridges` (pointeurs non possédants, durée de vie garantie par `AudioController`). `AudioRoutingRuntime::Create` crée les endpoints `wasapi:<id>:input|output:<n>` d'après `Health().channels` des ponts `Running`. `Process` appelle `ReadBlock` de chaque pont d'entrée dans `sourceBlocks_` avant `graph_->Process`, et `WriteBlock` de chaque pont de sortie depuis `destinationBlocks_` après. Les compteurs d'étape existants (`clientRead`, `routing`) ne changent pas de sens ; ajouter un compteur `bridgeCopy` dans `StageCounters` et dans la fenêtre de performance (`physical.bridgeCopy`, publié à la tâche 8).

- [ ] Tests : avec une interface de pont factice (classe de test implémentant les deux méthodes sur des vecteurs), vérifier que le signal d'un pont d'entrée arrive sur une entrée virtuelle et sur une sortie physique avec le gain attendu, que la sortie virtuelle arrive dans un pont de sortie, que `MatchesClientGenerations` reste vrai quand seuls les ponts changent, et qu'un pont renvoyant zéro frame produit du silence sans erreur.
- [ ] Construire, constater l'échec, implémenter, relancer `AudioRoutingRuntimeTests` et `PerformanceStageMeasurementsTests`. Commit : `Router les ponts WASAPI dans le runtime audio`.

### Tâche 7 : cycle de configuration et persistance dans le contrôleur

**Fichiers :** modifier `include/audio_controller.h`, `src/audio_controller.cpp`, `src/persistent_routes.h`, `tests/audio_controller_persistence_tests.cpp`, `tests/audio_routing_hot_swap_tests.cpp`, `tests/persistent_routes_tests.cpp`.

**Interfaces :** `AudioControllerSnapshot` gagne `std::vector<WasapiBridgeSnapshot> wasapiBridges` (configuration + `WasapiBridgeHealth` + endpoints). `ApplyConfigurationCore` suit la spécification §7.3 : validation complète avant mutation ; reconfiguration arrêtée si l'ensemble des ponts change (comparaison champ à champ hors `status`), permutation à chaud si seules les routes changent ; `wasapiBridges` absent du payload conserve les ponts courants ; les routes vers un pont `DeviceLost` ou un canal absent restent dans `configuredRoutes` avec la logique de `persistent_routes.h`. Le changement d'état d'un pont (notification hors temps réel) déclenche une recompilation à chaud et un événement.

- [ ] Tests de persistance : sauvegarde et rechargement avec ponts ; démarrage avec un pont dont le périphérique est absent et une route complète ailleurs donne `running` ; `[]` supprime les ponts, absence du champ les conserve.
- [ ] Tests de permutation à chaud : modifier uniquement les routes vers un pont ne passe pas par `reconfiguring` ; ajouter un pont y passe.
- [ ] Construire, constater l'échec, implémenter, relancer `AudioControllerPersistenceTests`, `AudioRoutingHotSwapTests`, `PersistentRoutesTests`. Commit : `Configurer et persister les ponts WASAPI dans le contrôleur`.

### Tâche 8 : serveur API

**Fichiers :** modifier `src/control_api_server.cpp`, `tests/application_profiles_api_tests.cpp` (ou nouveau `tests/wasapi_bridges_api_tests.cpp` sur le même modèle), `tests/api-runtime-smoke.ps1`.

**Interfaces :** `GET /api/v1/state` et `GET /api/v1/drivers` publient `wasapiDevices` et `wasapiBridges` ; `configuration.apply` lit `wasapiBridges`, traduit les erreurs de validation en codes §8.5 avec `operation`, `bridgeId`, `deviceId`, `hresult` ; événement `wasapi.bridge.status` à chaque changement d'état et chaque seconde en `running` ; `devices.changed` inclut les deux tableaux ; `audio.meter` couvre les endpoints WASAPI routés. Ajouter `physical.bridgeCopy` à `performanceMeasurements.window`.

- [ ] Tests d'API : état avec ponts conforme au schéma (valider avec le validateur déjà utilisé par les tests existants) ; `configuration.apply` avec un pont invalide renvoie le bon code sans mutation ; sans champ `wasapiBridges`, les ponts persistent ; événement émis sur changement d'état simulé.
- [ ] Mettre à jour `api-runtime-smoke.ps1` pour lire `wasapiDevices` et vérifier la présence du périphérique de rendu par défaut.
- [ ] Construire, constater l'échec, implémenter, relancer les tests d'API et `pwsh -NoProfile -File .\tests\api-contract.ps1`. Commit : `Exposer les ponts WASAPI dans l'API locale`.

### Tâche 9 : validation de bout en bout sur Windows

**Fichiers :** modifier `tests/with-loopback-route.ps1` (ou créer `tests/with-wasapi-route.ps1` sur le même modèle), `docs/TEST_VERIFICATION.md`, `docs/TEST_VERIFICATION.en.md`.

- [ ] Écrire un script qui configure, par WebSocket, un pont loopback sur le périphérique par défaut et une route vers un client TimoxVasio de test (`VasioExternalHostProbe` ou `vasio-probe`), lit un signal de test joué par `powershell` (`System.Media.SoundPlayer` ou un lecteur WASAPI minimal), et mesure sur 10 minutes : `measuredLatencyMs` dans `[consigne, consigne + période]`, `underruns = 0`, `overruns = 0`, `resyncs = 0`, `driftPpm` stable à ±10 ppm.
- [ ] Exécuter les quatre premiers cas d'usage de la spécification §1 avec Renoise ou Mixxx et un navigateur ; noter les latences et le temps de callback `physical.routing` avant et après ajout des ponts. Un écart supérieur au coût des copies d'anneau (vérifier avec `bridgeCopy`) est un échec.
- [ ] Documenter la procédure dans `TEST_VERIFICATION.md` et `.en.md`. Commit : `Vérifier les ponts WASAPI de bout en bout`.

### Tâche 10 : interface Timox VASIO Control

**Fichiers :** modifier `gui/src/configuration-merge.js`, `gui/src/configuration-merge.test.js`, `gui/src/RoutingPatchbay.js`, `gui/src/RoutingPatchbay.test.js`, `gui/src/App.js`, `gui/src/App.test.js`, `gui/src/route-visuals.js`, `gui/src/ProfessionalTheme.css`, `gui/src/BufferTimingDiagram.js`.

**Comportement (spécification §9) :**

- L'onglet `ASIO setup` est renommé `Audio setup`. Sous la carte ASIO, un tableau « Windows Audio bridges » : identifiant, type, périphérique, état, latence mesurée, dérive. Ce tableau est un état global ; il n'y a pas d'autre entrée de navigation.
- Dans le patchbay, un pont `running` est un nœud comme un processus, icône selon le type, nom du périphérique en sous-titre ; un pont `deviceLost` ou `error` est grisé avec ses routes configurées en pointillé.
- Menu contextuel du fond : `Add Windows Audio bridge…`. Menu d'un nœud de pont : `Edit…`, `Disable`/`Enable`, `Remove`. Formulaires en modale avec : type, sélecteur (liste `wasapiDevices` filtrée par flux, plus trois entrées « Default device » par rôle), mode, canaux, latence cible, filtre par processus (saisie libre du nom d'exécutable ; la liste des sessions est une question ouverte de la spécification §13, ne pas l'implémenter). Les actions de câble restent dans le menu du câble.
- Aucun panneau permanent ; sélectionner un pont ou un câble n'ouvre rien.
- Section Performance : une carte par pont (latence mesurée contre consigne, dérive, underruns, overruns, resyncs, « conversion par Windows » ou « par le pont ») et une étape « Windows audio bridge » dans le schéma des buffers.
- `configuration-merge.js` fusionne `wasapiBridges` comme les routes (par `id`, conflit si modifié ailleurs), et omet le champ si le brouillon n'a pas touché aux ponts (règle §8.3).

- [ ] Tests React d'abord : rendu du tableau, nœud de pont dans le patchbay, ouverture des modales depuis les menus contextuels uniquement, absence de tout panneau latéral après sélection d'un pont ou d'un câble (assertion explicite sur le DOM), fusion des ponts, carte Performance.
- [ ] `npm run react-test -- --watchAll=false --runInBand` avec `CI=true` ; constater l'échec ; implémenter ; relancer ; `node --check electron/main.js`.
- [ ] Prendre deux captures (patchbay avec un pont loopback et un pont rendu ; section Performance) dans `docs/images/` au format des captures 1.2.6, pour la revue. Commit : `Piloter les ponts WASAPI depuis Control`.

### Tâche 11 : vasio-doctor et documentation de phase 1

**Fichiers :** modifier `tools/vasio-doctor/vasio-doctor.ps1`, créer `tools/vasio-doctor/WasapiCapabilities.ps1` et un test Pester `tests/vasio_doctor_wasapi_capabilities.Tests.ps1` ; modifier `docs/ARCHITECTURE.md`, `docs/STRUCTURE.md`, `docs/FONCTIONS.md`, `docs/README.md` racine (tableau des alternatives : colonne « Audio Windows (WDM) » passe à « Oui, ponts WASAPI »), `docs/ROADMAP.md` et tous leurs `.en.md`.

- [ ] Test Pester : le contrôle signale la version de Windows, la disponibilité du loopback par processus (build ≥ 19041) et, pour le périphérique choisi, l'autorisation du mode exclusif ; fixtures pour un Windows ancien et un périphérique exclusif interdit.
- [ ] Implémenter le contrôle ; relancer Pester.
- [ ] Mettre à jour la documentation : diagramme d'architecture avec les ponts, structure des fichiers, guide des fonctions (captures de la tâche 10), README et limites connues (retirer « ne gère pas l'audio des applications Windows classiques », ajouter la limite « pas d'endpoint virtuel visible par Windows, utiliser un câble virtuel tiers »). Retirer l'entrée de la feuille de route ou la réduire aux phases 2 et 3. Commit : `Documenter les ponts WASAPI de phase 1`.

## Phase 2 : horloge maître WASAPI exclusive

### Tâche 12 : source de cadence abstraite et hôte exclusif

**Fichiers :** créer `include/clock_host.h`, `include/wasapi_exclusive_host.h`, `src/wasapi_exclusive_host.cpp` ; modifier `include/physical_asio_host.h` (hériter de `ClockHost`), `include/audio_controller.h`, `src/audio_controller.cpp`, `src/control_api_server.cpp`, `tests/wasapi_bridge_probe.cpp` (`--master`), `tests/audio_controller_persistence_tests.cpp`.

**Interfaces :** `ClockHost` de la spécification §6.2. `WasapiExclusiveHost::start` initialise un flux exclusif événementiel à `sampleRate` et `bufferFrames`, gère `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED` par recalcul de la période, publie la taille effective, et appelle le `PhysicalAsioProcessCallback` sur son thread MMCSS avec les canaux du périphérique comme entrées ou sorties « physiques ». `AudioController` choisit l'hôte selon `physicalDriverId` et le pont `clockRole: "master"` ; les capacités publiées dans `physicalCapabilities` sont celles du périphérique exclusif. Perte du périphérique maître : `engine.error` `WASAPI_DEVICE_LOST` puis état `error`.

- [ ] Tests : validation §8.2 des combinaisons maître (déjà couverte à la tâche 4, étendre si besoin) ; persistance d'une configuration sans carte ; test du contrôleur avec un `ClockHost` factice vérifiant que `AudioRoutingRuntime` reçoit les canaux du maître et que les autres ponts démarrent en esclaves.
- [ ] Construire, constater l'échec, implémenter, relancer. Sur Windows : `WasapiBridgeProbe --master --exclusive` sur un casque USB, puis moteur complet sans carte ASIO avec Renoise et Mixxx routés l'un vers l'autre et vers le casque, 10 minutes sans incident. Commit : `Faire d'un périphérique WASAPI exclusif l'horloge du moteur`.

### Tâche 13 : interface et documentation de phase 2

**Fichiers :** modifier `gui/src/App.js`, `gui/src/App.test.js`, `docs/API.md`, `docs/ARCHITECTURE.md`, `docs/INSTALL.md`, `docs/FONCTIONS.md`, `README.md` et leurs `.en.md`.

- [ ] Tests React : dans `Audio setup`, la carte ASIO accepte « Aucune (horloge WASAPI) » ; la modale de pont propose le rôle d'horloge uniquement pour `render`/`capture` en `exclusive` ; la section Performance nomme la source d'horloge.
- [ ] Implémenter ; relancer les tests GUI.
- [ ] Documenter le mode sans carte ASIO (prérequis : exclusivité autorisée dans Windows), mettre à jour l'installation (« un pilote ASIO physique » n'est plus un prérequis) et le README. Commit : `Documenter le mode sans carte ASIO`.

## Hors plan

- Phase 3 (endpoint virtuel visible par Windows, pilote KS signé) : non couverte, voir spécification §12.
- Numérotation 1.3, notes de version, Setup, ZIP, empreintes : tâche séparée du mainteneur après validation.
- Les trois questions ouvertes de la spécification §13 : conserver les choix par défaut indiqués dans ce plan (saisie libre du nom de processus, latence bornée à une période minimum, pas de gain par pont) et les signaler au mainteneur à la fin de la phase 1.

## Rapport attendu à la fin de chaque tâche

Dans le message de commit ou le compte rendu : commandes exécutées et résultats (succès ou échec, avec la sortie en cas d'échec), ce qui n'a pas pu être exécuté et pourquoi (par exemple, absence de Windows), mesures relevées (latence, dérive, temps de callback), et toute divergence par rapport à la spécification.
