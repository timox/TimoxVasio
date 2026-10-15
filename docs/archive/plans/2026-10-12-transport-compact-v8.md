# Transport compact v8 — Implementation Plan

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../../README.md) et la documentation courante qu'il référence.

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Copier seulement les voies ASIO créées par client en gardant la capacité annoncée de 256 voies et la cadence des callbacks.

**Architecture:** Un contrôle v8 stable publie la génération courante. Chaque génération a son propre mapping audio, ses listes de voies et ses strides immuables. La DLL publie hors callback ; le moteur construit et publie le runtime avant d’acquitter. L’ancien mapping reste vivant jusqu’à la quiescence.

**Tech Stack:** C++17, Win32 shared memory et Interlocked, MSVC x64 Release, CMake, API documentée.

---

### Task 1: Contrat API

**Files:** `API.md`, `tests/api-contract.ps1` si nécessaire.

1. Remplacer la mention des 256 slots fixes par la distinction capacité 256 / voies ASIO créées / slots transportés. Les slots ne dépendent pas des routes.
2. Documenter les compteurs `audio.meter` comme cumulatifs pendant la vie du contrôle client, y compris à travers les générations audio.
3. Garder epoch et acquittement privés ; ne créer aucun champ visible inutile. Vérifier le contrat concerné avant le code.

### Task 2: Contrôle et ABI v8

**Files:** `include/audio_transport.h`, `src/audio_transport_layout.h`, `src/audio_transport.cpp`, `src/audio_transport_status_reader.cpp`, `tests/audio_transport_probe.cpp`.

1. Définir en test le rejet des versions/taille/identité/epoch invalides et des listes non triées, dupliquées ou incohérentes avec les strides.
2. Créer un petit contrôle stable identifié par pilote, PID et création du processus, avec état/format, compteurs cumulatifs, epoch publié et acquitté.
3. Créer un mapping audio distinct par epoch : deux listes triées de voies ASIO 0..255, deux strides 0..256, deux anneaux et quatre index en trames initialisés à zéro.
4. Initialiser complètement avant publication release ; lire par snapshot acquire et valider magic, version, taille, identité, listes et strides. Ajouter les assertions ABI x64 utiles.
5. Faire lire le statut depuis le contrôle v8 ; retirer le repli audio v6/v7. Exécuter le probe ciblé.

### Task 3: Anneaux compacts

**Files:** `src/audio_transport.cpp`, `tests/audio_transport_probe.cpp`.

1. Couvrir 0, 2, 8 et 256 voies par sens, voies clairsemées dont 255, sens asymétriques, bouclage, sous-alimentation, saturation et identité des échantillons.
2. Adresser les anneaux par `frame × stride` et copier une ou deux portions contiguës. Le stride zéro fait progresser les index en trames sans copie. Remplir le silence manquant selon le stride.
3. Vérifier seulement le probe transport ciblé.

### Task 4: DLL ASIO

**Files:** `src/vasio_com_driver.h`, `src/vasio_driver.cpp`, `tests/driver_audio_probe.cpp`.

1. À `createBuffers`, préparer listes, tables inverses et staging compact hors callback, puis publier une nouvelle génération.
2. Exiger arrêt et `join` effectif de l’ancien thread avant remplacement ou `disposeBuffers` ; `running == false` ne suffit pas.
3. Dans `start`, attendre hors callback l’acquittement du même epoch et format avec délai borné et erreur explicite.
4. Dans `callbackLoop`, indexer les slots précompilés. Préserver appel hôte, signal et position temporelle ; aucune allocation ou attente nouvelle dans le callback.
5. Exécuter le probe DLL ciblé sans toucher aux sessions audio actives.

### Task 5: Moteur et bascule

**Files:** `include/vasio_client_manager.h`, `src/vasio_client_manager.cpp`, `include/audio_routing_runtime.h`, `src/audio_routing_runtime.cpp`, `src/audio_controller.cpp`, `tests/audio_routing_runtime_tests.cpp`, `tests/audio_routing_hot_swap_tests.cpp`.

1. Détecter un nouvel epoch même pour un PID déjà connu ; ouvrir et valider le nouveau mapping sans acquitter trop tôt.
2. Préparer hors callback les index slot ↔ endpoint et les buffers `frames × stride`. Garder les échanges et signaux des clients attachés sans route.
3. Réconcilier aussi sur changement d’epoch quand les routes sont identiques ; publier le nouveau runtime et attendre la quiescence physique avant de libérer l’ancien.
4. Acquitter seulement l’epoch du runtime publié. Vérifier identité des échantillons, absence de route et retraite de l’ancien runtime.

### Task 6: Vérification et documentation

**Files:** `docs/REPRISE_1_1_1_TEST_2026-10-11.md`, `docs/MESURES_1_1_1_TEST_2026-10-11.md` si de nouvelles mesures sont obtenues.

1. Compiler DLL et moteur ensemble en MSVC x64 Release dans les chemins canoniques ; relever chemin, date et SHA-256.
2. Exécuter seulement les probes et contrats API concernés. Ne pas lancer de charge ni remplacer les binaires des sessions audio actives.
3. Comparer le volume copié théorique aux 187,5 Mio/s par client du format 256. Revendiquer un gain CPU seulement si une mesure comparable l’établit.
4. Documenter résultats et limites ; vérifier diff et `git status` avant commit. Exclure `tests/results/`.
