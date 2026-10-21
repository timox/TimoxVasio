# Vérification du projet de pont WASAPI — 21 octobre 2026

> Revue historique du document ecdd878, conservée pour traçabilité. Le mainteneur a ensuite précisé l'objectif de moteur commun ASIO/WASAPI et autorisé le démarrage. Le [plan réorganisé](2026-10-21-wasapi-bridge.md) et la [nouvelle conception](../specs/2026-10-21-unified-audio-engine-design.md) remplacent les décisions de périmètre et numéros de tâches ci-dessous. Les corrections techniques WASAPI restent applicables.

## Provenance et verdict

Document demandé retrouvé par `git log --all` au commit `ecdd878`, présent dans `main`, mais absent de la branche de travail `ccr-d306084d-ieaii9`. Lecture par `git show`, sans checkout, merge ni modification de code. Spécifications française et anglaise présentes dans ce même commit. Base de code examinée : `f8984be824197c326d2ea88fdcd5be5949282c8f`.

**Verdict : architecture générale pertinente, plan non exécutable en l'état.** Les anneaux entre le moteur et les threads WASAPI sont la bonne frontière. Les corrections ci-dessous doivent être intégrées avant le code correspondant. Le [plan révisé](2026-10-21-wasapi-bridge.md) les prend en compte ; il ne constitue pas une validation des performances annoncées.

Trois sous-agents ont participé : flux et frontière du pilote (`audio-signal-flow-explainer`), sûreté audio (`audio-dsp-review`), contrat et documentation (`api-documentation-generator`). Synthèse et plan réalisés avec `writing-plans`.

## Autonomie du pilote : exigence retenue

`TimoxVasio.dll` reste le pilote ASIO virtuel autonome en tant que composant : même identité, mêmes clients, même transport, aucune ouverture de périphérique WASAPI, aucun rééchantillonneur ni inventaire Windows Audio dans la DLL. Les ponts sont facultatifs et appartiennent exclusivement au moteur. Le fonctionnement existant sans pont reste disponible.

Cette autonomie ne signifie pas aujourd'hui une cadence indépendante du moteur ou du matériel : `src/vasio_driver.cpp` (`start`, `createBuffers`, `callbackLoop`) attend l'attachement, la génération et les signaux du moteur ; `src/audio_controller.cpp` impose actuellement une horloge physique. La phase 2 remplace cette horloge par WASAPI exclusif, elle ne crée pas de fonctionnement sans périphérique. Une horloge logicielle autonome serait une troisième conception distincte, non déduite de la demande.

## Corrections requises

| Priorité | Constat vérifié | Correction intégrée au plan |
|---|---|---|
| Bloquant | Tâche 5 mélange conversion automatique Windows et `InitializeSharedAudioStream`. Cette méthode documente `EVENTCALLBACK` comme drapeau pris en charge [M1]. | Séparer ouverture classique avec conversion Windows et ouverture `IAudioClient3` au format négocié avec conversion dans le pont. Loopback : chemin classique explicite. |
| Bloquant | Loopback par processus annoncé à partir de 19041, alors que l'exemple Microsoft exige 20348 et ne lie pas la capture à un endpoint [M2]. | Détecter la capacité réelle, corriger le seuil et distinguer le sélecteur processus du sélecteur MMDevice. |
| Bloquant | `Clear`/`Prefill` pendant une perte de périphérique peut réinitialiser un anneau utilisé simultanément par le callback. | Générations immuables d'anneaux, retrait après acquittement du runtime ; aucune remise à zéro concurrente. |
| Bloquant | Phase 2 promet des IDs `wasapi:*` stables mais transforme le maître en canaux `physical:*`. `ClockHost` ne couvre pas les opérations actuellement requises par le contrôleur. | Adaptateur d'horloge complet, propriétaire unique du flux exclusif, mapping WASAPI explicite dans le runtime. |
| Bloquant | Passage à `apiVersion: 1.1` sans modifier `gui/src/api-contract.js`, qui refuse toute version autre que `1.0`, ni le producteur de version dans `src/control_api_server.cpp`. | Migration coordonnée schéma, serveur, adaptateur GUI et fixtures ; lecteur GUI accepte 1.0 et 1.1. |
| Majeur | Pointeurs de pont non possédants sans protocole de retraite ; faux pont de test non défini dans l'interface proposée. | Définir interface RT injectable et possession par génération du runtime. Aucun destructeur COM sur callback. |
| Majeur | Ratio `mix/graph` sans convention de direction ; régulateur sans signe ni essai de dérive opposée. | Ratio sortie/entrée, capture `graph/device`, rendu `device/graph`, tests ±100/±1000 ppm dans les deux directions. |
| Majeur | Un remplissage hors `[0, capacité]` ne peut pas déclencher la reprise d'un anneau correctement borné. | Déclencher sur discontinuity, starvation durable ou saturation ; distinguer silence normal, incident et reprise. |
| Majeur | Flux de rendu silencieux présenté comme obligatoire pour le loopback. Microsoft indique que le contournement ne concerne que les versions antérieures à Windows 10 1703 [M3]. | Événements natifs sur la base Windows retenue ; test du silence et watchdog sans seconde horloge du graphe. |
| Majeur | « Latence mesurée » calculée par occupation de l'anneau, confondue avec une mesure de bout en bout. | Exposer une estimation explicitement nommée ; mesurer séparément par corrélation, incluant rééchantillonneur, WASAPI, graphe et transport. |
| Majeur | Promesse de CTest hors Windows alors que CMake arrête la configuration hors Windows et exige l'ASIO SDK ; projet en C++17, pas C++20. | Conserver C++17 ; prévoir un mode CMake tests purs avant les vérifications Windows/SDK et enregistrer explicitement les nouveaux tests. |
| Majeur | Smoke test exigeant un périphérique de rendu par défaut ; sonde exigeant zéro incident même lors d'un débranchement volontaire. | Fixtures sans périphérique ; séparer tests de stabilité et tests de récupération. |
| Majeur | `Health().channels` peut changer lors d'une reconnexion tandis que le runtime garde ses buffers. | Format et nombre de canaux figés par génération ; recompiler hors RT et conserver les routes absentes. |
| Mineur | Fichiers de persistance divergents, `docs/README.md` inexistant, métrique `StageCounters` imprécise. | Utiliser le store existant, `README.md` racine et les compteurs réellement déclarés dans les en-têtes. |

Le fichier de persistance réellement choisi par `AudioConfigurationStore::DefaultPath()` est `configuration.json`, et non `audio-configuration.json`. Ne pas changer son emplacement ni son nom à l'occasion du pont. Les nouvelles dépendances moteur doivent également être reportées dans les cibles de tests dont les listes de sources sont dupliquées dans CMake.

`tests/api-contract.ps1` utilise effectivement `Test-Json` pour les fixtures ; les tests Node de `api-contract.test.cjs` vérifient l'adaptateur JavaScript, pas le JSON Schema. Les deux sont nécessaires.

## Décisions proposées pour fermer les questions ouvertes

- Première livraison : ponts esclaves, ASIO maître inchangé. Deuxième livraison distincte : horloge WASAPI exclusive.
- Saisie libre du processus ; pas d'inventaire de sessions ni de gain par pont ; gain existant par route.
- Latence demandée 1–500 ms, cible effective au moins égale au maximum d'une période WASAPI et d'un bloc du graphe ; publier les deux valeurs.
- Aucun nouvel endpoint Windows, pilote kernel, installateur ou changement de version produit dans ce travail.
- L'interface montre les ponts dans le contenu de `Audio setup` et dans le graphe, jamais dans la navigation gauche. Menus contextuels puis modales seulement.

Ces décisions sont des propositions du plan, pas des choix déjà confirmés par le mainteneur.

## Sources Microsoft consultées

- [M1 — InitializeSharedAudioStream : drapeaux et contraintes de période](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream).
- [M2 — Exemple ApplicationLoopback : build 20348 et capture indépendante de l'endpoint](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/ApplicationLoopback/README.md).
- [M3 — Loopback Recording : événements depuis 1703 et mode partagé obligatoire](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

## Validation de cette revue

Lecture des documents historiques, des sources actuelles, des tests et des références Microsoft. Aucun build, test audio, installation ou changement de processus : la demande porte sur la vérification documentaire et le plan. Les performances et la compatibilité matérielle restent à mesurer lors de l'exécution.
