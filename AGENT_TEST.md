# Instructions agent — Banc de test et compatibilité de TimoxVasio

## Rôle et objectif

Tu construis un banc de test pour **TimoxVasio**, le pilote ASIO virtuel du projet Mixette SCEngine (Windows). Sa compatibilité n'est aujourd'hui que théorique : il faut la mesurer.

Le cœur du travail est un **hôte ASIO de test** (`vasio-probe`) qui charge le pilote comme le ferait un logiciel de musique, l'interroge, le fait tourner, et produit des mesures objectives. Il sert ensuite à comparer TimoxVasio avec une carte son physique.

Tu ne modifies pas le pilote. Tu signales chaque défaut trouvé avec sa reproduction minimale.

## Livrables

| Fichier | Contenu |
| --- | --- |
| `tools/vasio-probe/` | Hôte ASIO de test en C++ (64 bits), en ligne de commande |
| `tools/vasio-probe/README.md` | Compilation, options, exemples |
| `tests/run_matrix.ps1` | Script qui enchaîne les scénarios automatiques et range les résultats |
| `tests/results/` | Un rapport JSON par exécution + un résumé Markdown |
| `docs/TESTS_HOTES_REELS.md` | Procédure manuelle pour Renoise, Mixxx, SuperCollider |
| `tools/vasio-status/` | Affichage et enregistrement en lecture seule de l'état partagé publié par chaque client TimoxVasio |
| `tools/vasio-doctor/` | Diagnostic indépendant et en lecture seule de l'environnement ASIO, audio Windows et MIDI |

## Règles impératives

1. **SDK ASIO** : référence-le par un chemin configurable. Ne copie aucun fichier du SDK dans le dépôt : sa licence en interdit la redistribution.
2. **Callbacks temps réel** : dans `bufferSwitch` / `bufferSwitchTimeInfo`, aucune allocation, aucune entrée-sortie, aucun verrou bloquant. Les mesures vont dans un tampon circulaire sans verrou ; un thread séparé les écrit sur disque.
3. **Horloge de référence** : `QueryPerformanceCounter` pour tous les horodatages.
4. **Droits administrateur** (enregistrement COM, base de registre HKLM) : demande à l'utilisateur, ne contourne pas.
5. **Valeurs de seuil** : dans un fichier de config, jamais en dur. Les seuils proposés ci-dessous sont des points de départ.
6. Avance par étapes testables, dans l'ordre des modes ci-dessous. Ne passe au mode suivant que si le précédent fonctionne.
7. La DLL reste minimale : seul ajout autorisé, publier son propre état dans la zone partagée sans verrou bloquant. Aucun inventaire du système, aucune ouverture d'un autre pilote, aucune écriture disque depuis la DLL. `vasio-status` et `vasio-doctor` effectuent les analyses hors du pilote.

## L'hôte de test `vasio-probe`

Fonctions de base, communes à tous les modes :

- Lister les pilotes ASIO déclarés dans le registre (`HKLM\SOFTWARE\ASIO`) et charger celui demandé par son nom.
- Gérer les messages du pilote via `asioMessage` : demande de réinitialisation, changement de taille de buffer, changement de latence, resynchronisation, support du `TimeInfo`. Chaque message reçu est journalisé.
- Produire un rapport JSON horodaté : pilote testé, paramètres, mesures, verdict par critère.

### Mode 1 — `caps` : capacités déclarées

Interroger et consigner :

- nombre d'entrées et de sorties, nom et type d'échantillon de chaque voie ;
- tailles de buffer : minimum, maximum, préférée, granularité ;
- fréquences acceptées parmi 44 100, 48 000, 88 200, 96 000, 176 400, 192 000 Hz ;
- latences d'entrée et de sortie déclarées ;
- source d'horloge déclarée.

Signaler toute incohérence (taille préférée hors bornes, type d'échantillon différent entre voies, fréquence acceptée puis refusée à l'usage).

### Mode 2 — `lifecycle` : cycle de vie

- Répéter N fois (défaut : 1 000) : init → création des buffers → start → quelques buffers → stop → libération → déchargement.
- Variante : décharger puis recharger la DLL dans le même processus.
- Mesurer : plantages, blocages (délai maximal par étape), mémoire et handles du processus avant/après.

### Mode 3 — `cadence` : régularité et dérive

Faire tourner le pilote pendant une durée donnée (défaut : 60 min) et mesurer :

- **intervalle entre callbacks** comparé à la période théorique (taille de buffer / fréquence) : moyenne, min, max, 99e centile ;
- **callbacks manqués ou en double** ;
- **position d'échantillon** (`ASIOGetSamplePosition` ou `TimeInfo`) : doit être strictement croissante, par pas égaux à la taille de buffer ;
- **dérive** : fréquence réelle déduite de la position d'échantillon comparée à `QueryPerformanceCounter`, exprimée en ppm.

### Mode 4 — `loopback` : intégrité du signal (le test principal)

Deux instances de `vasio-probe` dans deux processus, comme deux logiciels de musique reliés par TimoxVasio :

- **Émetteur** : écrit sur les sorties de TimoxVasio un signal de test identifiable — une séquence pseudo-aléatoire dont chaque bloc porte un compteur d'échantillons.
- **Récepteur** : lit les entrées correspondantes de TimoxVasio et vérifie.

Mesurer :

- **identité bit à bit** entre émis et reçu (aucune altération du gain, aucune conversion de format cachée) ;
- **trous et doublons** : échantillons perdus ou répétés, détectés via le compteur ;
- **latence de transport** en échantillons, et sa stabilité dans le temps ;
- comportement quand le récepteur démarre après l'émetteur, et inversement ;
- comportement quand l'un des deux s'arrête brutalement (processus tué).

Variante mono-processus : si TimoxVasio permet de router une sortie vers une entrée du même client, faire le même test dans une seule instance.

### Mode 5 — `multiclient` : charge réaliste

- Lancer 2, 4, puis 8 instances simultanées (émetteurs et récepteurs mêlés), chacune sur ses propres voies.
- Ouvrir et fermer les instances dans un ordre aléatoire pendant que les autres tournent.
- Mesurer, pour les instances qui restent : trous, doublons, gigue des callbacks.

### Mode 6 — `reconfig` : changements à chaud

- Changer la fréquence d'échantillonnage, puis la taille de buffer, pendant que d'autres clients tournent.
- Vérifier que le pilote prévient les clients (message de réinitialisation) et que tous repartent sans plantage.

### Mode 7 — `drift-vs-hw` : TimoxVasio face à une carte physique

- Lancer simultanément une instance sur TimoxVasio et une sur la carte physique (deux processus, puisqu'un hôte n'ouvre généralement qu'un pilote ASIO à la fois).
- Comparer la progression des positions d'échantillon des deux pilotes sur 60 min.
- Résultat attendu : la dérive relative en ppm, et le temps au bout duquel l'écart atteint une taille de buffer (moment où un pont entre les deux sans rééchantillonnage commencerait à craquer).

## Matrice d'exécution automatique

`run_matrix.ps1` enchaîne les modes 1 à 6 sur TimoxVasio pour chaque combinaison :

Avant les modes, `run_matrix.ps1` lance `vasio-doctor` en lecture seule. Pendant les modes 3 à 6, il enregistre l'état de `vasio-status` sans modifier la configuration.

| Paramètre | Valeurs |
| --- | --- |
| Fréquence | 44 100, 48 000, 96 000 Hz |
| Taille de buffer | 64, 128, 256, 512 échantillons (limitées aux valeurs acceptées) |
| Nombre de clients | 1, 2, 4, 8 |
| Durée (modes 3 à 5) | Courte (5 min) pour le balayage, longue (60 min) sur les combinaisons retenues |

Le mode 7 se lance à part, la carte physique étant désignée par son nom de pilote dans la config.

## `vasio-status` (lecture seule)

Avant d'ajouter le bloc d'état, documenter la zone partagée actuelle et proposer sa structure. Le pilote y publie uniquement son état : version et chemin de sa DLL par client, PID, voies lues/écrites, état, dernier échange, fréquence et buffer demandés, compteurs et horodatages QPC, erreurs du transport et demandes de réinitialisation. L'outil lit ce bloc, le complète avec le nom du processus et le routage vus hors DLL, et calcule période, dérive et divergences. Affichage direct, `--json`, incohérences en rouge et historique facultatif. Il ne modifie aucune configuration.

## `vasio-doctor` (lecture seule)

Lançable à tout moment, il ne corrige rien automatiquement. Il examine les pilotes ASIO du registre (nom, CLSID, chemin DLL, présence, architecture 32/64 bits), les périphériques audio Windows (fréquence, matériel USB, version du pilote) et les processus qui tiennent une DLL ASIO. `--probe <pilote>` est refusé si le pilote est déjà utilisé ; sinon il réutilise `vasio-probe` pour un profil réel (capacités, cycle de vie court, cadence de 60 secondes, reconfiguration, seconde ouverture simultanée) et enregistre `tests/results/profiles/<carte>.json`.

Le diagnostic MIDI énumère les ports WinMM et Windows MIDI Services, identifie la pile utilisée, compare les ports attendus, signale les ports exclusifs occupés et propose `--listen <port> <secondes>` pour mesurer un MIDI clock entrant (tempo, gigue, Start/Stop). Verdict vert/orange/rouge selon la configuration attendue : fréquences cohérentes, matériel et ports présents, absence de conflit, une seule version de TimoxVasio et aucun client fantôme. Chaque ligne comporte une action corrective proposée, jamais appliquée.

## Critères de réussite proposés

| Critère | Seuil de départ |
| --- | --- |
| Identité bit à bit en bouclage | 100 % sur 60 min |
| Trous ou doublons | 0 sur 60 min |
| Gigue des callbacks (99e centile) | Inférieure à 25 % de la période de buffer |
| Cycle de vie | 1 000 cycles sans plantage, sans fuite de handles |
| Latence de transport | Constante sur toute la durée |
| Changements à chaud | Tous les clients repartent |
| Dérive face à la carte physique | Mesurée et documentée (pas de seuil : c'est une donnée d'architecture) |

## Partie manuelle — hôtes réels

Dans `docs/TESTS_HOTES_REELS.md`, une fiche par logiciel (Renoise, Mixxx, SuperCollider) :

- TimoxVasio apparaît-il dans la liste des pilotes ? Les voies sont-elles nommées correctement ?
- Fréquences et tailles de buffer proposées par le logiciel, et celles qui fonctionnent vraiment ;
- lecture de 30 min sans craquement, avec LatencyMon en parallèle pour écarter un problème système ;
- fermeture et réouverture du logiciel pendant que les autres tournent ;
- bouclage Mixxx → TimoxVasio → SuperCollider, vérifié à l'oreille et par `vasio-probe` en récepteur sur une voie libre.

## Points ouverts — à remonter, ne pas trancher seul

- Quelle source d'horloge TimoxVasio utilise-t-il pour cadencer ses buffers ?
- Comment TimoxVasio attribue-t-il les voies entre clients ? Routage interne d'une sortie vers une entrée possible ?
- Quelle carte physique sert de référence pour le mode 7 ?
- Faut-il aussi une version 32 bits de `vasio-probe` (anciens logiciels hôtes) ?
