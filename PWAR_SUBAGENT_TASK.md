# Mission du sous-agent PWAR

Consigne autonome préparée le 26 octobre 2026. Ce document remplace le transfert
d'une session : la session PWAR précédente ne peut pas être rattachée au parent.

## Instruction de lancement pour l'agent principal Timox

Créer **un seul** sous-agent nommé `pwar`, dans ton propre arbre d'agents, avec :

> Lis entièrement `C:/Users/timo/Documents/GitHub/TimoxVasio/PWAR_SUBAGENT_TASK.md`.
> Tu prends en charge la mission PWAR décrite dans ce fichier. Inspecte l'état
> courant avant toute action. Communique directement avec moi par les outils
> inter-agents, pas par une boîte aux lettres Markdown. Commence par analyser
> l'échec aller-retour documenté, puis propose et réalise une correction ciblée
> dans ton périmètre. Coordonne avec moi toute utilisation du moteur partagé.
> Ne crée pas d'autre sous-agent. Le contrat de supervision actuel doit rester
> compatible avec l'intégration Timox en cours.

Le parent conserve les composants Timox et centralise les opérations sur le
moteur partagé. Le sous-agent conserve PWAR. L'ancienne session ne poursuit
plus ses modifications ou essais en parallèle après ce transfert.

## Objectif complet

Une application Windows doit pouvoir envoyer son audio via Timox/PWAR vers
PipeWire Linux, y subir un traitement, récupérer le signal et le router dans
Timox, avec la carte ASIO physique toujours utilisée en parallèle.

Le résultat global comprend : audio simultané dans les deux sens, gestion
correcte canaux/fréquences/blocs PipeWire, configuration et supervision GUI/API
(activité, pertes, xruns, latence réelle), contrôle distant authentifié depuis
Linux, continuité, adaptation des horloges et reprise après déconnexion.

Le parent réalise GUI/API/authentification Timox. Le sous-agent réalise le
transport, le client PWAR et les interfaces nécessaires. Un client qui démarre
ou des tests unitaires verts ne constituent pas la réussite de cet objectif.

## Périmètre impératif

- Modifications autorisées : `patches/pwar/**`, documentation PWAR et sources
  PWAR consolidées dans `build_pwar_reconciled_20261025`.
- **Ne pas modifier** les autres composants Timox : moteur, pilote, GUI, API,
  CMake racine, WASAPI ou authentification. Signaler au parent tout besoin
  précis dans ces composants.
- Ne pas changer la carte ASIO physique pour le pilote PWAR historique.
  Le client autonome PWAR est une application du pilote virtuel Timox.
- Préserver les changements des autres agents. Lire AGENTS.md, confirmer la
  racine et lire git status avant toute compilation.
- Réutiliser les deux dossiers existants `build_pwar_reconciled_20261025` et
  `build_pwar_reconciled_tests`. Pas de nouvelle série de builds à la racine.
- Ne pas publier de PR upstream avant une stabilisation démontrée et accord
  du parent/utilisateur. Aucune PR PWAR n'est actuellement livrée par cette mission.
- Appliquer Ponytail : correction complète minimale, réutilisation des
  mécanismes existants, tests ciblés justifiés par le défaut. L'utilisateur
  refuse une campagne interminable de tests sans trajet complet fonctionnel.

## État livré et points d'entrée

- Sources du client Windows : `patches/pwar/client/asio_client.cpp`,
  `frame_ring.h`, `control_protocol.h` et tests associés.
- Sources réseau/Linux :
  `build_pwar_reconciled_20261025/linux/pwar_stream_relay.c`,
  `protocol/pwar_audio_queue.c`, `protocol/pwar_router.c`.
- Base upstream : `ed5b07c44c7635d006422d646d56368600504b3f`.
  Correctifs conservés dans `patches/pwar`. Les numéros 0008–0014 couvrent
  notamment fragments, séquences, queues et comptage du silence ; le backend
  stream et la capacité de réception Windows ont des patches séparés.
  Ne pas confondre le manifeste historique du bundle avec les candidats actuels.
- Binaire de développement :
  `build_pwar_reconciled_tests/Release/PwarAsioClient.exe`.
  SHA-256 vérifié le 26 octobre :
  `9E47FDE737D54552E0882D8FFF2D49AB912BDB92CE3247A39341634720CA8441`.
- Build ciblé existant :
  `cmake --build build_pwar_reconciled_tests --config Release --target PwarAsioClient`.
  Source CMake : `patches/pwar`, `PWAR_SOURCE_DIR` pointe vers les sources
  consolidées, SDK local `asiosdk`, JSON existant dans `vendor`.
- Contrat faisant autorité : `patches/pwar/client/CONTROL.md`.
  Arguments : `--driver-clsid`, `--peer`, `--max-queue-ms`, `--stdio`.
  JSON UTF-8 par ligne, version 1 ; commandes `status`/`stop`, événements,
  stdout et stderr drainés en continu, arrêt sur EOF stdin.
- CLSID virtuel : `{A4D39126-78CB-4D89-9E0A-54494D4F5856}`.
  DLL installée : `C:/Program Files/Steinberg/VirtualASIO/1.2.7/TimoxVasio.dll`.
- Limites présentes : stéréo float32 little-endian, 48 kHz, ports 8321 fixes,
  bloc ASIO préféré jusqu'à 4096 frames. Aucune compensation de dérive ou
  reprise automatique qualifiée. `latency_ms` et `drift_ppm` restent null.
  Le filtrage de l'adresse UDP ne constitue pas une authentification audio.

## Défaut prioritaire : aller-retour reproduit, mais discontinu

Le parent Timox a exécuté une observation avec vraie source et vrai récepteur
Windows, client PWAR et retour PipeWire. Pas de DSP ajouté pour cet essai.

Journaux à lire avant toute nouvelle exécution :
`build_pwar_reconciled_tests/pwar-observe-d2a0e288-831f-42ff-8d84-7d892fa23388/`.
Lire `receive.jsonl`, `send.jsonl`, `pwar.jsonl`, les requêtes/réponses API,
`before.json` et `after-cleanup.json`.

Résultats consignés par Timox et signal récepteur lus par l'ancienne session :

- 480 000 frames mesurées, 70 871 à zéro (14,8 %), 295 fenêtres de tonalité
  reconnues sur 400 ; test de présence échoué, pas de reset ni non-fini.
- Client : 354 callbacks, 5 664 paquets émis, 5 860 blocs reçus,
  tx_dropped_frames = 0, rx_expired_frames = 22 272, silenced_frames = 1 152.
- Dès le premier callback, rx_expired_frames était déjà 20 096 : ne pas
  attribuer toutes les expirations à la période utile du signal.
- Carte physique SSL conservée, 48 kHz, horloge ASIO. Le parent a confirmé
  50 routes identiques avant/après, révision 5 vers 7, aucune route temporaire
  restante et aucun processus Windows de test laissé actif.

Lecture du journal Linux après cet essai : rx_packets = 5 664,
played_frames = 608 000, rx_dropped_frames = 116 992, invalid_packets = 0.
5 664 × 128 = 724 992 = 608 000 + 116 992. C'est un indice fort de pertes
locales dans la file de réception Linux ; **ce n'est pas encore la cause
complète démontrée des coupures**. Comparer les deltas sur la fenêtre d'essai.
Les compteurs de silence Linux comprennent des heures d'attente sans source
Windows : leurs totaux ne mesurent pas les coupures pendant le test.

Pistes à vérifier : bursts de 16 paquets par callback ASIO de 2048 frames,
capacité Linux de 4096 frames, comportement skip_read/skip_write, amorçage,
cadence réelle des deux streams et retour. Ne pas augmenter aveuglément les
tampons ni masquer les silences pour faire passer le test.

## Environnement Linux et ressources partagées

- SSH par clé déjà configuré :
  `ssh -F C:/Users/timo/.ssh/timoxvasio_config timox-linux`.
  Alias vers `timo@10.1.0.101`, Windows observé `10.1.0.104`.
  Ne pas recopier de secrets dans le dépôt. Si sandbox refuse l'accès,
  demander l'escalade via les outils ; ne pas contourner les permissions.
- PipeWire observé : 1.0.5. Graphe matériel précédemment 44,1 kHz/1024,
  adaptateurs de streams négociés en stéréo 48 kHz ; revalider au besoin.
- Répertoire existant : `/tmp/timox-pwar-qualification.HYVsLU`.
  Sources : `reconciled/build_pwar_reconciled_20261025`.
  Build : `reconciled-build`, exécutable `linux/pwar_stream_cli`.
  Headers isolés : `headers`, qualification ASAN/UBSAN : `reconciled-audit`.
- Dernier relais **vérifié vivant le 26 octobre** : PID 3467277,
  `--ip 10.1.0.104 --buffer_size 128`, écoute UDP 8321.
  Journal : `coordinated-roundtrip.log` dans le répertoire ci-dessus.
- Derniers liens vérifiés : `pwar-receive:output_FL` → `pwar-send:input_FL`,
  FR → FR. Revalider avant d'utiliser ; ne pas lier aux haut-parleurs.
- Une ancienne source de tonalité `timox-qualification-source` peut encore
  tourner, sans lien PWAR au dernier contrôle. Ne pas l'utiliser comme preuve
  d'un signal provenant de Windows.
- L'ancienne session SSH interactive n'est pas transférable au nouveau
  sous-agent : ouvrir sa propre connexion et vérifier processus/ports.

## Séquence de travail attendue

1. Accuser réception au parent par `send_message`, confirmer le périmètre et
   annoncer l'analyse du défaut documenté. Pas de nouvelle boîte aux lettres.
2. Analyser code et journaux existants ; exposer une hypothèse causale précise
   et le plus petit contrôle permettant de la confirmer ou de l'écarter.
3. Corriger PWAR, conserver un correctif reproductible dans `patches/pwar`,
   exécuter les seuls tests pertinents pour la modification.
4. Demander au parent un créneau exclusif pour moteur/routage/UDP 8321 et
   annoncer exactement qui lance l'observation. La disponibilité n'est pas
   déduite d'un ancien PID ou d'un message sans accusé.
5. Réutiliser `patches/pwar/client/observe-roundtrip.cjs` et la sonde existante.
   Le script ajoute deux routes temporaires avec contrôle de révision, retire
   uniquement ses routes et enregistre l'état après nettoyage.
6. Si le trajet fonctionne, ajouter un traitement Linux identifiable et les
   validations ciblées de dérive/reprise/durée. Ne pas confondre reconnaissance
   de tonalité, continuité et qualité audio. Ne pas déclarer l'objectif global
   atteint tant que GUI/API/authentification et critères audio manquent.

Pour la dérive, lire `patches/pwar/PIPEWIRE_CLOCK_ADAPTATION.md` : RateMatch SPA
existe dans les headers 1.0.5, tandis que pw_stream_set_rate requiert 1.4.0.
La réception effective de RateMatch et le fonctionnement dans les deux sens
ne sont pas encore démontrés. Réutiliser PipeWire plutôt qu'inventer un DSP.

## Retour attendu au parent

Rapporter par message direct : cause établie ou hypothèse encore ouverte,
fichiers/patches modifiés, binaire et empreinte, scénario exécuté, résultat
mesuré, nettoyage et ressources encore actives, limites restantes.
En cas de besoin Timox : indiquer la commande/API ou le comportement exact
requis, l'entrée, la sortie attendue et un cas reproductible. Ne pas attendre
en silence ni multiplier les tests pour contourner une dépendance au parent.

Les anciens fichiers HANDOFF_TIMOX.md, REPLY_TIMOX.md et PWAR_REPRISE.md sont
des archives/contextes. Le transfert présent remplace la coordination par
fichier ; les outils inter-agents du parent sont désormais le canal de travail.
