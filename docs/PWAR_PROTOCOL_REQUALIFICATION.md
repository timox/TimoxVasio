# Requalification du protocole PWAR

État au 24 octobre 2026 : audit en cours, protocole non qualifié.

## Objectif conservé

Intégrer une liaison audio bidirectionnelle Windows/Linux compatible PipeWire,
fondée sur PWAR, configurable et supervisable par l'API Timox. La carte ASIO
locale doit rester utilisable simultanément. La qualification finale couvre
les canaux, fréquences et quanta, l'état réel et les diagnostics API, le contrôle
distant authentifié, la stabilité sur deux machines, la dérive et la reprise.

PWAR transporte les canaux de son endpoint ASIO explicitement ouvert et routé.
Il ne découvre ni ne capture les flux arbitraires des processus Windows ; seuls
les ports raccordés à cet endpoint appartiennent au graphe Timox.

La demande complémentaire impose de vérifier et consolider le protocole avant
de revendiquer des optimisations. Aucun succès local ne remplace cette qualification.

Direction explicitement retenue : évolution du moteur Timox vers une compatibilité
PipeWire pratique, et non portage du moteur PipeWire sous Windows. Le réseau doit
devenir une entrée/sortie du graphe, indépendante de la carte ASIO locale.

## Preuves exécutées

Banc indépendant : `patches/pwar/protocol-audit`, compilant le véritable
`protocol/pwar_router.c` de la copie suivante :

`build_native_production/pwar-lifecycle-verification/PWAR-ed5b07c44c7635d006422d646d56368600504b3f`.

SHA-256 du routeur testé :
`3B97A6F0FD04546F98CEBAA6BB9C37F3B8A5DFF2419BDE0A85AB4204506B31C5`.

Cette copie comporte des correctifs locaux ; ce hash ne désigne pas l'amont vierge.

Commande : `ctest --test-dir build_pwar_protocol_audit -C Release --output-on-failure`.
Résultat initial : **0/4**, quatre violations reproduites sans réseau ni pilote :

| Invariant | Résultat observé | Cause dans le routeur |
|---|---|---|
| Aller-retour exact de 192 frames en fragments de capacité 128 | Retourne 128 frames, échantillons différents | Offset et longueur totale calculés avec la taille du dernier fragment |
| Un bloc complet n'est livré qu'une fois | Le doublon relivre 128 frames | Le test de complétude reste vrai après livraison |
| Métadonnées de fragmentation cohérentes dans une séquence | Le fragment annonçant 3 paquets au lieu de 2 modifie l'état | Aucun contrat de séquence mémorisé pour ce champ |
| Un fragment ancien ne détruit pas l'assemblage courant | Séquence 19 remplace l'assemblage de la séquence 20 | Toute différence de séquence déclenche une réinitialisation |

Ces tests doivent devenir verts après consolidation, sans inverser leurs attentes.
Ajouter ensuite permutations, pertes, duplications, changements de quantum,
wraparound, redémarrage du pair et validation des dimensions et de l'état.

### Premier correctif de consolidation : 0008

`patches/pwar/0008-router-preserve-fragment-integrity.patch` conserve la structure
des paquets UDP et corrige le réassemblage. Les fragments sont stockés dans des
emplacements fixes et assemblés dans leur ordre logique, avec une longueur de
dernier fragment distincte. Un bloc n'est livré qu'une fois. Les dimensions et
le nombre de fragments sont validés avant mutation. Les séquences anciennes
ne remplacent plus une séquence active.

Le banc étendu comporte dix cas : quatre régressions initiales, six permutations
d'un bloc de 320 frames, streaming depuis zéro avec doublons, bouclage 64 bits,
perte suivie d'une nouvelle séquence, réinitialisation explicite du pair, rejet
d'une capacité insuffisante sans changement d'état. **Témoin : 2/10 ; candidat :
10/10.** Les cas perte et réinitialisation passaient déjà sur le témoin.

La suite existante, reconstruite contre le candidat dans
`build_pwar_protocol_qualification`, passe **22/22**, incluant le callback Linux
isolé (avec stubs), la DLL réelle et l'hôte ASIO Timox. Ce n'est toujours pas
une qualification Linux/PipeWire réelle. La compilation signale encore des
avertissements du pilote/SDK et des conversions de télémétrie ; le nouveau
routeur n'a pas émis d'avertissement.

Le patch a été appliqué à une copie distincte avec `git apply
--ignore-space-change --directory=build_pwar_protocol_patchcheck` depuis la
racine Timox ; les deux fichiers obtenus correspondent aux sources compilées
après normalisation CRLF/LF. Depuis une vraie racine PWAR, omettre `--directory`.

Limites explicites : le protocole n'a pas d'identifiant de session. Après un
redémarrage distant, le propriétaire doit réinitialiser le routeur ; la détection
et la reprise réseau restent à implémenter. Le bouclage testé est celui du champ
64 bits, alors que Linux utilise encore un compteur 32 bits et Windows tronque
la séquence de réponse en 32 bits. Ces deux usages restent à corriger et tester.
Les changements de quantum au milieu d'un assemblage restent un chantier distinct.

Deux régressions supplémentaires ont ensuite échoué avant correction : le
paquet généré dépendait des octets préexistants dans son tampon (canal absent,
queue et padding), et l'émetteur acceptait plus de 64 fragments, au-delà de la
capacité du récepteur. Le patch 0008 initialise maintenant chaque paquet entier
et refuse ces dimensions avant écriture. Cela couvre `pwar_router_send_buffer`,
pas encore les deux constructions manuelles de paquets dans `libpwar.c`.

Suite intégrée finale de cette passe : **34/34**, 2,42 s, le 24 octobre 2026.
Le CMake principal de qualification inclut désormais `protocol-audit`.
Patch 0008 SHA-256 :
`BBBC96517634463D3BE7E6198F31B1D3B8CD7BDE674A2BC767832E4A53D0E01C`.
DLL candidate : `build_pwar_protocol_qualification/Release/PWARASIO.dll`,
64000 octets, horodatage UTC `2026-10-24T20:24:38`, SHA-256
`67D2A086426D5CA535618F89494525154D6649C0A222DBB01A5A1C459F493DA0`.
Application finale du patch et comparaison des deux sources effectuées dans
`build_pwar_protocol_patchcheck_final`. Aucun enregistrement ni installation.
Portée importante : le cas routeur `partial` (192 frames assemblées à partir
de 128+64) appelle directement `pwar_router_process_packet`. Il ne traverse
pas le listener `pwarASIO::udp_packet_listener`, qui recalcule encore
`num_packets` depuis `blockFrames / chunk_size`, rejette un reste non nul et
copie `blockFrames` même si le routeur fournit moins de `samples_ready`. Le
34/34 prouve donc le réassemblage du routeur, pas la fragmentation du trajet
réseau vers un bloc ASIO. Un test intégré du listener reste nécessaire.
Le manifeste du bundle à sept patches reste inchangé ; ce candidat n'y est
pas inclus et ne doit pas être confondu avec cette distribution antérieure.

## Constats statiques supplémentaires à reproduire

### Deuxième consolidation : 0009 et premiers essais Linux

`0009-preserve-frame-cursor-and-response-sequence.patch` corrige le curseur du
tampon de réception Linux (frames consommées au lieu de nombre de blocs multiplié
par le quantum courant), valide ses arguments et conserve les 64 bits de la
séquence dans la réponse du pilote Windows. Les cinq nouveaux cas échouent avant
correction et passent ensuite : croissance/réduction du quantum, fin partielle,
arguments invalides et réponse de la DLL au passage de `UINT32_MAX`.

Suite Windows : **39/39**, 2,48 s. Application du patch vérifiée sur une copie
distincte ; sources comparées après normalisation des fins de lignes et de la
dernière ligne vide. Empreinte du patch :
`5ECB89193C7454F2437FCB2445CD5234C5B5B2BA46262C1B433E2584EB8EABF6`.
DLL candidate à neuf patches : `build_pwar_protocol_qualification/Release/PWARASIO.dll`,
64000 octets, UTC `2026-10-24T20:31:48`, SHA-256
`DDE605417A33EE40445BB0FAC0FF0DD969F4100C816A71B2657F6836E879585C`.

Un hôte Ubuntu 24.04 avec GCC 13.3, PipeWire 1.0.5 et WirePlumber actifs est
désormais disponible par SSH. Le graphe annonce 48 kHz, quantum par défaut 1024,
minimum 32, maximum 2048. Le paquet PWAR 0.1.0 et sa GUI sont déjà présents ;
la GUI occupe UDP 8321. Ils n'ont pas été remplacés ni arrêtés.

Dans un répertoire temporaire isolé, les **16 tests protocol-audit passent sous
GCC avec AddressSanitizer et UndefinedBehaviorSanitizer** (0,07 s). L'archive
transférée a été comparée par SHA-256 :
`4E87BD42DDDEA270F1E8E93F3F3887C0D336E1C89FC67D1F223B0C48720A2364`.
Les en-têtes PipeWire/SPA ont été téléchargés via apt puis extraits localement,
sans installation système. La compilation du relais nécessite un chemin de
bibliothèques explicite pour cette disposition isolée.

Cette passe ne qualifie pas encore la cadence du callback PipeWire ni le transport
audio sur le réseau. Le compteur émetteur Linux 32 bits, les paquets construits
manuellement, l'état global du tampon et les appels bloquants restent à traiter.

### Constats restant ouverts

- Le format transmis est une structure C brute : contrat d'offsets, padding,
  endianness, représentation float et taille à documenter et tester sur les
  deux plateformes. Pas de version/type explicite dans l'en-tête audio observé.
- Les tableaux d'échantillons sont planaires malgré le commentaire « interleaved ».
- `linux/libpwar.c` initialise seulement le canal 0 dans les paquets locaux
  de `stream_buffer` et `process_ping_pong`, puis transmet toute la structure.
  Le second canal et les zones inutilisées doivent être déterministes.
- Le callback appelle `sendto`, prend des mutex, écrit des diagnostics et,
  en oneshot, attend une condition avec un délai nominal de 2 ms.
  Un bloc de 64 frames à 48 kHz ne représente que 1,333 ms.
- `pwar_rcv_buffer.c` emploie un état global et calcule sa position comme
  `chunk_pos * chunk_size` : les changements de quantum doivent être testés,
  notamment lorsque la nouvelle position dépasse les frames disponibles.
- La réception Linux met à jour la télémétrie avant validation du routeur et
  ne récupère pas l'identité de l'émetteur dans le `recvfrom` examiné.
- Le patch Linux 0007 évite certaines copies invalides mais ne transporte pas
  les quanta supérieurs à 128 : silence, donc objectif fonctionnel non atteint.

## Ordre de consolidation

1. Définir les invariants de séquence et de fragmentation compatibles avec les
   émetteurs existants ; distinguer streaming et séquence fragmentée. Vérifier
   explicitement les conditions de reprise après redémarrage et wraparound.
2. Corriger réassemblage, données non initialisées et validation des entrées,
   avec tests de régression sur la véritable implémentation.
3. Retirer réseau, attentes et mutex du callback PipeWire ; préparer des files
   bornées et définir la politique de pertes et de silence.
4. Traiter la stéréo et les quanta variables sur le chemin Linux réel.
5. Ajouter le pont indépendant de la carte ASIO et l'adaptation des horloges.
6. Exposer configuration, compteurs et états par API, puis le contrôle distant
   authentifié prévu dans les documents existants.
7. Mesurer sur deux machines et sous fautes : précision du signal, latence
   p50/p95/max, CPU, pertes, xruns, dérive, arrêts et reprises.

## Optimisation : hypothèses à mesurer

Découpler les attentes réseau du callback est prioritaire. Mesurer ensuite
copies, utilisation des buffers, cadence réseau et coût de réassemblage.
Le format fixe transmet la capacité de 128 frames même pour un bloc plus court :
réduire ce coût changerait potentiellement le contrat de transport et exige une
stratégie de compatibilité explicite, pas une modification silencieuse.

Aucune amélioration CPU ou de latence n'est encore mesurée. Ne pas supprimer
de contrôles ni réduire les buffers pour obtenir artificiellement un benchmark.

## Mesure intermédiaire du 25 octobre : réception des rafales

Qualification **non acquise**. Le backend expérimental `pw_stream` utilise les
adaptateurs PipeWire pour un transport stéréo 48 kHz sur le graphe Linux réel
44,1 kHz / 1024 frames. Il est conservé dans
`patches/pwar/experimental-pipewire-stream-backend.patch` ; il ne remplace pas
encore le backend livré et n'est pas cumulable tel quel avec les patches
0011–0013, qui doivent être réconciliés.

Le tampon UDP Windows de 1024 octets ne peut absorber correctement les rafales
de paquets de 1064 octets. `windows-receive-burst-capacity.patch` demande 1 Mio
et vérifie l'échec de `setsockopt`. Cette capacité ne constitue pas une garantie
de latence : la politique de rejet des paquets trop anciens reste à qualifier.

Après compilation, les **40 tests Windows passent**, dont une rafale de
16 paquets avec callback volontairement retardé. Le signal de ce test reste
dans la plage audio pour ne pas confondre écrêtage et corruption. Le relais
Linux doit être suspendu pendant les tests isolés utilisant le même port 8321.

Essai réel de 60 secondes, source Linux 440 Hz, retour Windows L=0,5*x et
R=-0,5*x, enregistré sous Linux :

| Mesure | Avant capacité augmentée | Après |
|---|---:|---:|
| Callbacks Windows | 6 732 | 22 672 |
| Coupures de silence d'au moins 64 frames dans le signal | 278 | 3 |
| Plus longue coupure | 26 809 frames | 19 626 frames (409 ms) |
| Valeurs non finies enregistrées | 0 | 0 |
| Erreur maximale L+R | 0 | 0 |

Après correction : étendue active 61,098125 s, RMS gauche 0,0351660,
crête gauche 0,0550246. La capture comprend du silence avant/après l'hôte ;
les coupures comptées excluent ce silence extérieur. Les compteurs du relais
sont cumulatifs et ne permettent pas d'attribuer seuls les pertes à cet essai.
Ces mesures montrent une amélioration, pas une continuité validée.

DLL testée : `build_pwar_protocol_qualification/Release/PWARASIO.dll`, SHA-256
`65A3D5D4881447CDAAF7C575C7EC9D4834AC6A1F1D2A0ACA2E5CA29D75B37337`.
Restent notamment : origine des trois coupures, latence, dérive sur longue
durée, reprise après coupure réseau, coexistence ASIO matériel et intégration
API/GUI. Une PR upstream reste conditionnée à une qualification concluante.
