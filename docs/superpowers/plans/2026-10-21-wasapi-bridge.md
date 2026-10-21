# Moteur de routage ASIO/WASAPI — plan d'implémentation

> **For agentic workers:** utiliser superpowers:subagent-driven-development ou superpowers:executing-plans, tests ciblés avant code, revue indépendante après chaque incrément.

**Goal:** construire sous Windows un moteur de routage commun aux applications ASIO et WASAPI, dans l'esprit fonctionnel de PipeWire, avec TimoxVasio comme accès ASIO virtuel indépendant des backends.

**Architecture:** le moteur possède le graphe et la politique de cadence ; ASIO et WASAPI raccordent applications et périphériques. La DLL conserve son identité et son transport sans ouvrir de périphérique WASAPI. Les frontières entre horloges utilisent des anneaux préalloués et une conversion adaptative hors callback du graphe.

**Tech Stack:** C++17, Windows Core Audio/MMCSS, SDK ASIO existant, CMake, React/Electron et API HTTP/WebSocket.

**Spec:** [conception du moteur commun](../specs/2026-10-21-unified-audio-engine-design.md). [Revue historique](2026-10-21-wasapi-bridge-review.md). Ce plan remplace le séquencement « carte ASIO obligatoire puis WASAPI exclusif ». Les documents de ecdd878 restent des références corrigées par la revue.

## Contraintes globales

- Identité ASIO, CLSID, profils, maximum de 256 canaux et transport préservés. Aucun backend WASAPI/SRC dans TimoxVasio.dll.
- Un seul propriétaire de la cadence du graphe. Le mode ASIO existant reste disponible ; pas de bascule implicite entre maîtres.
- WASAPI partagé sans carte ASIO est un usage central ; exclusif optionnel ultérieur.
- Pas d'allocation, attente, verrou, COM, log ou destruction de session dans le callback du graphe.
- Persistance dans configuration.json au chemin existant. Anciennes configurations compatibles ; champs absents conservés, suppression explicite et révision transactionnelle.
- UI : navigation globale seulement ; menus contextuels puis modales ; sélection seule sans ouverture de panneau.
- Avant build : git rev-parse --show-toplevel et git status --short. Réutiliser le générateur du cache.
- Aucun packaging, installation, arrêt de processus utilisateur, nouvelle dépendance ou version produit implicite.
- Documentation produit française/anglaise synchronisée ; tests matériels distincts des tests factices.
- Exigence mainteneur : GUI unifiée, couverture API et performances au moins au niveau actuel de TimoxVasio. Qualifier cette parité par usage et par chemin audio, pas seulement par compilation.

## Review Focus

1. Régression ASIO : propriété worker/COM, format et compteurs conservés — incrément 1.
2. Deux cadences simultanées ou callback après Stop — incréments 1–2.
3. Dérive opposée, starvation et anneau réinitialisé concurremment — incréments 3–4.
4. Révision périmée ou périphérique absent effaçant les routes — incréments 5–6.
5. Confusion loopback/entrée virtuelle Windows ou exclusif/partagé — incréments 4, 6–8.

## Livraisons

| Incrément | Résultat vérifiable | Dépend de |
|---|---|---|
| 1 | Contrôleur découplé de l'hôte ASIO concret, tests sans carte, frontière DLL protégée | existant |
| 2 | Graphe cadencé sans carte ASIO, configuration d'horloge explicite | 1 |
| 3 | Anneaux, générations et conversion adaptative | 1 |
| 4 | Backends WASAPI partagés capture/rendu/loopback | 3 |
| 5 | Graphe commun et transactions de backends | 2, 3, 4 |
| 6 | API et Control pour les usages ASIO↔WASAPI | 5 |
| 7 | Qualification interapplications et câble virtuel tiers | 6 |
| 8 | Horloges WASAPI matérielles et endpoints Windows natifs, lots séparés | 7 |

## Incrément 1 — Première tranche autorisée

**Fichiers :** include/audio_clock_host.h, include/physical_asio_host.h, include/audio_controller.h, src/audio_controller.cpp, tests/audio_controller_clock_tests.cpp, CMakeLists.txt ; tests/driver_import_policy.ps1, tests/verify_driver_boundary.ps1, tests/wasapi_driver_boundary.Tests.ps1.

**Interface :** AudioClockHost expose les opérations déjà utilisées : enumerate, open/close, setSampleRate, refreshCapabilities, start/stop, notifications et compteurs. Types neutres device/capabilities/callback/timing ; noms PhysicalAsio* conservés comme aliases. PhysicalAsioHost implémente l'interface. Fabrique optionnelle au constructeur du contrôleur, appelée sur son worker ; défaut ASIO inchangé.

- [x] Écrire un test du contrôleur réel avec hôte factice : énumération, négociation, fermeture, restart, rejet de fréquence non confirmée et taille invalide ; fabrique nulle/échouée proprement gérée.
- [x] Constater le rouge, implémenter l'interface et l'injection sans changement du JSON/API ni du transport. Tests de données audio conservés dans les suites runtime existantes ; ne pas inventer de routes physique→physique interdites.
- [x] Protéger la frontière DLL : sources/dépendances autorisées, fixtures refusant backend/SRC sans interdire ole32 légitime. Audit statique conservateur, pas prétention d'interpréter tout CMake.
- [x] Construire moteur, AudioControllerClockTests, AudioControllerPersistenceTests, ApplicationProfilesApiTests et régressions runtime pertinentes ; lancer tests frontière/imports.
- [x] Revue indépendante puis journal des résultats. Cet incrément ne rend pas encore le moteur utilisable sans matériel : il crée la séparation testable nécessaire.

## Incrément 2 — Cadence indépendante de la carte

**Fichiers :** include/software_clock_host.h, src/software_clock_host.cpp, tests/software_clock_host_tests.cpp ; contrôleur, runtime, store, schémas/API et tests.

**Contrat :** clockSource discriminé asio/software/futur wasapi, distinct de l'inventaire de périphériques. Ancien fichier sans champ garde la sémantique ASIO. Fréquence et quantum explicites ; format effectif confirmé avant publication et acquittement clients.

- [x] Fixer les transitions de maître : arrêter, retirer l'ancien callback, préparer la génération puis démarrer. Pas de cadence de secours implicite.
- [x] Tests temps simulé : deadlines absolues monotones, dépassement sans rafale illimitée, aucun callback concurrent/après Stop.
- [x] Implémenter cadence Windows annulable, worker MMCSS et buffers préalloués ; pas de temporisation relative accumulant la dérive.
- [x] Autoriser le mode software sans endpoints physiques ; tester application→application avec clients synthétiques, clients sans routes, arrêt/reprise et générations. Qualification avec applications réelles encore requise.
- [ ] Tester anciennes/nouvelles configurations, puis qualifier la cadence réelle. Aucune modification d'ABI partagée sans nécessité démontrée et revue.

## Incrément 3 — Frontières entre horloges

**Fichiers :** src/spsc_audio_ring.h, src/drift_resampler.h/.cpp, include/wasapi_bridge_io.h, tests dédiés et CMake.

**Interfaces :** Write/Read retournent des frames réellement transférées ; ReadBlock complète de silence. SRC retourne consumed/produced. Ratio sortie/entrée : capture graph/device, rendu device/graph ; correction ±0,2 %. Le retour sur remplissage est négatif dans les deux directions : capture dF/dt = D×ratio−G ; rendu dF/dt = G−D/ratio. Pour une même dérive matérielle, les corrections stabilisées sont opposées. Ne pas inverser arbitrairement le signe du PI (correction de la proposition historique).

- [x] Tests motifs multicanaux concurrents, wrap-around, transferts partiels, compteurs et zéro allocation.
- [ ] Générations possédées : préparation/préremplissage hors RT, publication, retraite après acquittement. Jamais Clear/Prefill sur un anneau actif.
- [ ] Tests SRC 44.1↔48 kHz et ratio 1 : SNR >90 dB à 1/10 kHz après compensation du retard, anti-alias, frames ±1 après vidage ; dérive ±100/±1000 ppm, paquets irréguliers.
- [x] Implémenter sinc polyphasé/PI anti-windup, figer les gains à partir des tests. Mode CTest pur avant les gardes Windows/SDK et POST_BUILD Windows. Qualification des gains avec paquets réels encore nécessaire.

## Incrément 4 — WASAPI partagé

**Fichiers :** include/wasapi_device_enumerator.h, include/wasapi_bridge.h, src correspondants, tests/wasapi_bridge_backend_tests.cpp et tests/wasapi_bridge_probe.cpp.

**Interfaces :** énumérateur Enumerate/Resolve ; pont Start/Stop/Health hors RT ; WasapiBridgeIo par génération au format immuable. Backend injectable pour les erreurs et notifications.

- [ ] Tests format refusé, absence de périphérique, silence, changement de canaux, invalidation, arrêt pendant activation asynchrone ; vérifier propriété COM, buffers et handles.
- [ ] Séparer ouverture IAudioClient::Initialize avec conversion Windows et IAudioClient3 au format supporté avec SRC pont. Ne pas mélanger les drapeaux incompatibles recensés dans la revue.
- [ ] Loopback endpoint partagé ; processus par activation distincte sans selector MMDevice (capacité documentée build 20348 et vérification effective). Identité PID/date de création.
- [ ] Reprise annulable avec backoff 250/500/1000/2000/5000 ms et nouvelle génération ; esclave perdu isolé du graphe.
- [ ] Sonde capture/rendu/loopback : stabilité séparée du débranchement volontaire. Pas de test contrat exigeant un périphérique par défaut réel.

## Incrément 5 — Graphe et transactions

**Fichiers :** routing_graph, audio_routing_runtime, contrôleur, stores, persistent_routes et suites associées.

- [ ] IDs stables wasapi:<id>:input|output:<n>, bindings possédant leur génération. Sources vers destinations, y compris WASAPI→WASAPI pour le moteur commun ; restrictions ASIO historiques inchangées jusqu'à extension explicite.
- [ ] Refuser un retour direct loopback endpoint→même rendu résolu. Ce contrôle ne prétend pas prévenir toutes les boucles interapplications/acoustiques.
- [ ] Lire sources avant graphe, écrire destinations après ; route seule permutée à chaud, changement de format préparé hors RT.
- [ ] Champ absent résolu dans la transaction, [] suppression, revision périmée sans mutation, rollback des préparations échouées, routes absentes persistantes.
- [ ] Mettre à jour toutes les cibles partageant les sources moteur, pas seulement l'exécutable. Tester perte/reprise d'un backend sans arrêt du maître courant.

## Incrément 6 — API et Control

**Fichiers :** schémas/OpenAPI, serveur, gui/src/api-contract.js, configuration-merge.js, App.js, RoutingPatchbay.js, métriques et tests.

- [ ] Migrer version émise, schéma, fixtures et adaptateur ensemble ; lecture GUI 1.0/1.1. Test-Json valide les messages complets, tests Node l'adaptateur.
- [ ] Publier clockSource/capacités séparés de physicalCapabilities ; format, latence demandée/effective/estimée, correction ppm et incidents.
- [ ] Audio setup dans le contenu ; menus contextuels/modales ; start/stop/reset indépendants de physicalDriverId.
- [ ] Fusion par ID/révision, événements sans écrasement du brouillon, suppression backend/routes atomique.
- [ ] Tests contrats/React, react-build et revue visuelle grand/petit écran.

## Incrément 7 — Qualification des usages

- [ ] ASIO→ASIO sans carte ; loopback→ASIO ; ASIO→rendu partagé ; ASIO→câble virtuel→microphone choisi par une application.
- [ ] 44.1/48 kHz, plusieurs quanta, 10 minutes stables après chauffe ; récupération testée séparément et routes préservées.
- [ ] Latence par corrélation de signal connu ; distinguer estimation de buffers et mesure. Comparer distributions p50/p95/max callback avec/sans pont.
- [ ] Documentation produit FR/EN et limites. Scripts réversibles, restauration conditionnée à la révision et arrêt limité aux processus du test.

## Incrément 8 — Extensions distinctes

Horloge WASAPI matérielle/exclusif : propriétaire unique du flux, alignement effectif avant clients, IDs WASAPI conservés, aucun SRC contre sa propre horloge, perte maître contrôlée. Concevoir après qualification du partagé.

Endpoints Windows natifs : pilote audio virtuel, distribution/signature en projet distinct. Rendu pour entrer dans le graphe, capture pour sortir vers les applications ; réutiliser le contrat moteur sans transformer TimoxVasio.dll en pilote Windows Audio. Le câble tiers permet de qualifier ces usages avant ce lot.

## Exécution et preuve

Travail dans la branche de session existante ; sous-agents spécialisés et revue indépendante. Pas de modifications concurrentes contrôleur/CMake. Chaque incrément : test rouge → code → tests verts → revue.

Build natif après contrôle racine/statut : cmake --build build_codex_110 --config Release --target <Cible>, puis exécutable build_codex_110/Release/<Cible>.exe ; lire codes retour et sorties. Les tests matériels sont notés non exécutés tant qu'ils ne le sont pas.

[Journal d'avancement](2026-10-21-wasapi-bridge-progress.md). L'incrément 1 est vérifié ; les incréments 2–4 sont en cours de qualification/intégration. Les suivants restent à implémenter.
