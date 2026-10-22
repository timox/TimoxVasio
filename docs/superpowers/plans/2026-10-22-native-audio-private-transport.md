# Native audio private transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** connecter les endpoints natifs au moteur par un transport privé borné, avec isolation du contrôle et retrait sûr.

**Architecture:** un objet de contrôle distinct des objets audio publics reçoit des IOCTL copiés. Une session propriétaire unique porte une génération monotone et deux files PCM préallouées. Le noyau conserve son horloge ; les transferts utilisateur ne pilotent pas la progression WaveRT.

**Tech Stack:** C++17 sans allocation dans le cœur partagé ; PortCls/WaveRT, WDK épinglé, METHOD_BUFFERED, tests CTest hôte.

**Spec:** [native Windows audio](../specs/2026-10-22-native-windows-audio-driver.md), contrat N0 dans `native-windows-audio/include/`.

## Global Constraints

- PCM signé 32 bits, 48 kHz, deux canaux ; maximum 4096 frames par paquet.
- Aucun chargement du pilote sur le poste actuel ; aucune modification de signature/Secure Boot.
- `TimoxVasio.dll` conserve ses interfaces et son autonomie.
- Aucune attente/allocation dans les callbacks audio ; aucune adresse utilisateur sérialisée.
- La qualification noyau reste ouverte en l'absence de cible récupérable.

## Review Focus

- Second handle ou jeton périmé : aucune lecture/injection possible.
- Reconnexion après crash : ancienne génération refusée et aucune donnée précédente livrée.
- Retrait PnP pendant transfert : refuser les nouveaux échanges, terminer les références avant libération.
- Saturation/paquet incomplet : politique explicite de perte, silence et compteurs.
- Entrées tronquées/direction incorrecte : aucun accès avant validation de la taille et du contexte.

## Décision de contrôle et sécurité

Créer un **device object de contrôle séparé**, avec nom propre Timox et classe
GUID propriétaire ; ne pas attacher le contrôle à l'objet audio World RW.
Utiliser `IoCreateDeviceSecure` et `FILE_DEVICE_SECURE_OPEN`, ACL laboratoire
`D:P(A;;GA;;;SY)(A;;GA;;;BA)` ; pas de droit World/Authenticated Users.
Le contrôle privé de production sera ouvert par un service broker dédié,
avec ACL fondée sur son SID et politique inter-session explicite. Ce lot ne
rend ni Electron ni le moteur ordinaire administrateur permanent.

Le dispatch distingue l'objet de contrôle ; les IRP audio continuent vers
PortCls. CREATE réserve une seule session par FILE_OBJECT ; un identifiant
interne non nul sert uniquement à reconnaître cette référence, jamais à
authentifier une valeur reçue dans un paquet. CLEANUP invalide cette session,
vide les files et termine les requêtes en attente ; CLOSE libère le contexte
quand plus aucune référence ne subsiste. La génération est allouée par le
pilote, jamais demandée par le client ; elle ne boucle pas après UINT64_MAX.

METHOD_BUFFERED, droits READ|WRITE obligatoires, codes complets vérifiés.
OPEN retourne la génération ; WRITE_CAPTURE copie un paquet N0 validé,
READ_RENDER retourne un paquet N0 validé et sa taille réelle ; STATUS expose
les compteurs. Maximum 32832 octets par paquet. Première version synchrone
non bloquante : file vide retourne un état explicite, aucune IRP pendante.
Cela réduit le périmètre d'annulation initial ; une attente asynchrone future
devra passer par une file annulable et ses tests dédiés. Aucun polling dans
le callback audio ; le worker moteur possède sa cadence.

STOP/retrait suspend les admissions avant d'attendre les références hors DPC.
La fermeture purge les deux files sous la même exclusion que les transferts.
Une primitive de rundown protège la vie de l'adaptateur indépendamment du
verrou court des files. Aucun code ne libère un ring alors qu'un callback
possède sa référence. Les règles pures ci-dessous ne remplacent ni les ACL
ni la synchronisation WDK.

Sources Microsoft vérifiées : [IoCreateDeviceSecure, GUID de classe propre](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdmsec/nf-wdmsec-wdmlibiocreatedevicesecure),
[dispatch PortCls](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/portcls/nf-portcls-pcdispatchirp),
[rundown non paginé jusqu'à DISPATCH_LEVEL](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-exacquirerundownprotection),
[IoDeleteDevice différé jusqu'aux dernières références](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iodeletedevice).
Le sample sélectionné refuse déjà un second adaptateur via un compteur
Interlocked ; N2 conserve explicitement cette limite. Le contrôle appartient
à cet adaptateur. La table des dispatch audio originaux est initialisée une
fois dans DriverEntry avant création de devices, puis reste immuable.

## Task 1 — Session et génération pures

**Files:** créer `native-windows-audio/include/native_audio_session.h`,
`native-windows-audio/tests/native_audio_session_tests.cpp`, modifier CMake.

**Interfaces:** `Session::Open(owner, generation)`, `Allows(owner,generation)`,
`Close(owner)`, `Remove()`. Owner est un entier opaque interne non nul.
Toutes méthodes sont sérialisées par l'appelant. Remove est terminal.

- [x] Test rouge : owner nul, double open, mauvais owner, mauvaise génération,
  fermeture étrangère sans effet, reopen génération incrémentée, remove actif,
  overflow génération terminal et sorties inchangées sur échec.
- [x] Implémentation sans OS/allocation/atomiques ; aucune prétention d'authentification.
- [x] Build puis CTest (4/4 avec les primitives de file).
- [x] Revue indépendante et validation du lot par root avant commit intermédiaire.

## Task 2 — Files PCM bornées et validation intégrée

**Files:** `native-windows-audio/include/native_audio_frame_queue.h`,
`native-windows-audio/include/native_audio_packet_queue.h`,
`native-windows-audio/tests/native_audio_queue_tests.cpp`.
Deux files distinctes, chacune limitée à 4096 frames PCM32 stéréo ; stockage
préalloué fourni par le propriétaire. `PushCapture` valide N0 et la session
avant copie ; `ReadRender` construit un paquet avec génération courante.
Rejeter un paquet capture entier si place insuffisante ; render plein perd
les nouvelles frames et incrémente un compteur, sans attendre. Capture vide
rend du silence. Discontinuité indiquée après perte ou reconnexion.

- [x] Tests rouge puis vert : roundtrip PCM, wrap, plein/vide, silence partiel,
  argument invalide sans mutation, purge de la file.
- [x] Primitive FrameQueue sans allocation/OS, sérialisation externe documentée.
- [x] Wrapper : paquet N0 invalide sans mutation, mauvais propriétaire,
  purge atomique session/files et reopen ; génération sur chaque transfert.
- [x] Tests hôte 5/5 : render vide sans paquet/silence, sortie trop petite
  sans consommation, séquence croissante/retry après Full, troncatures et
  256 versions majeures invalides.
- [x] Implémenter copies bornées et compteurs ; sérialisation externe documentée.
- [x] Tests hôte et compilation kernel d'un consommateur réel ; revue statique.
- [x] Lot vérifié par root pour commit intermédiaire ; qualification noyau distincte.

## Task 3 — Dispatch sécurisé et raccordement WaveRT

**Files:** `driver/control.cpp/.h`, adapter/stream/projet, probe utilisateur.
Avant code, vérifier les contrats WDK officiels pour création sécurisée,
dispatch PortCls, rundown et retrait PnP ; consigner les liens exacts.

- [x] Coder device distinct, session unique, IOCTL bornés sans IRP pendante.
- [x] Raccorder render/capture sous verrou court et référence de durée de vie.
- [x] Compiler WDK/InfVerif/ApiValidator ; tests purs (CTest 5/5).
- [x] Préparer/compilier probe ouverture, second handle, paquet tronqué,
  major READ non supporté, close/reopen ; **non exécuté** sur pilote.
- [x] Revue statique indépendante : garde IRQL ajoutée avant région critique
  et rejet immédiat des majors privés non supportés ; aucun autre P1/P2 relevé.
- [ ] Étendre/exécuter qualification non-admin, kill en charge, STOP/retrait,
  IRQL/Driver Verifier sur cible récupérable avant tout déploiement.

Limites de cette tranche : compteurs de pertes hors session et timestamps QPC
des paquets render restent à compléter ; aucune parité de latence n'est
mesurée. Le broker utilisateur et la capture effective entre applications
ne sont pas encore intégrés. OPEN/CLOSE sont réalisés par CREATE/CLEANUP ;
SESSION retourne la génération, sans créer une deuxième session.
