# Native Audio Authenticated Channel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Relier le transport client existant à `BrokerSession` par de vrais pipes Windows, chiffrés et authentifiés avec Schannel, sans ouvrir le pilote pendant les tests de ce lot.

**Architecture:** Le bootstrap authentifie le compte et son logon, puis SESSION réserve le propriétaire. `ConfirmLease` déplace cette même connexion logique sur un pipe de données distinct, réauthentifié et lié au bail. Le client conserve `INativeAudioAuthenticatedChannel` ; le serveur utilise un seul `BrokerSession` pour arbitrer toutes les connexions.

**Tech Stack:** C++17, Win32 named pipes overlapped, Schannel/SSPI, certificats CNG fournis par le provisioning, CTest/MSVC `/W4 /WX`.

**Spec:** [Plan production N4](2026-10-22-native-audio-production.md), [contrat client](../../../include/native_audio_broker_transport.h), [prototype Schannel](../../../native-windows-audio/schannel-probe/README.md).

## Global Constraints

- Aucune activation dans le contrôleur, aucun chargement du pilote, service SCM ou provisioning de certificat dans ce lot. Aucun fallback clair, direct N2 ou compte privilégié.
- Bootstrap `\\.\pipe\Timox.NativeAudio.v1`, local seulement, maximum quatre connexions ; aucune donnée PCM sur ce pipe. Une requête audio outstanding par connexion de données.
- Audio : enveloppe existante de 48 octets, payload maximum 32832, message maximum 32880 ; aucune modification implicite du codec SESSION existant.
- TLS 1.2 minimum, AEAD, certificats client et serveur obligatoires, pins/validité/EKU explicitement vérifiés. Aucun certificat reçu ne devient une autorité de confiance.
- Stockage applicatif total borné à 256 KiB par connexion logique, y compris les deux pipes pendant le transfert et les buffers détenus côté canal ; comptabiliser séparément le stockage global de `BrokerSession` et les allocations internes opaques de Schannel.
- Délai absolu fourni par le client : Open partage déjà cinq secondes entre Connect, SESSION et ConfirmLease. Le serveur borne également à cinq secondes le bootstrap et la confirmation, sans prolongation à chaque fragment reçu.
- Toutes les opérations appartiennent aux workers ; aucune nouvelle opération de transport dans le callback audio. RequestCancel est sticky et la seule opération concurrente du client.
- Les tests synthétiques n'attribuent jamais `isolationQualified` à une machine réelle. L'admission par défaut reste refusée.

## Review Focus

- EOF ou révocation entre SESSION et confirmation : retirer le bail sans laisser un propriétaire orphelin (tâche 4).
- Annulation pendant connexion, handshake ou écriture partielle : observer la completion avant toute libération, même si la cancellation perd la course (tâche 1).
- Records TLS et messages applicatifs fragmentés/coalescés : aucune confusion de longueur ni perte de données restantes (tâche 2).
- Proxy relayant TLS depuis un autre logon : comparer identité observée et identité liée, pas seulement le certificat (tâche 3 ; qualification inter-logons séparée).
- Rotation de confiance pendant une réponse préparée : annuler, retirer puis supprimer la réponse non envoyée avant une nouvelle admission (tâche 4).

## Décisions de liaison à conserver

`Connect` termine TLS sur le bootstrap puis échange une liaison chiffrée avec lease/generation zéro. Le serveur relève le jeton après lecture, par `ImpersonateNamedPipeClient`, copie SID utilisateur, SID de logon, AuthenticationId et TokenSessionId, puis appelle immédiatement `RevertToSelf`. Il compare le SID utilisateur à l'association du certificat autorisé. Le client compare la réponse à son propre jeton. Échec d'identification : fermeture ; échec de retour d'identité : arrêt du traitement privilégié, pas de poursuite sous impersonation.

Après SESSION réussi, le serveur crée le pipe de données avant de proposer son transfert. La réponse audio SESSION reste inchangée (génération seulement). Une **trame de contrôle du canal**, chiffrée et distincte de l'enveloppe audio, transporte l'offre ; `ConfirmLease` la consomme. Aucun opcode d'authentification n'est ajouté au codec audio.

Format de contrôle v1 proposé : magic distinct `TNAC`, version u16, type u16, payloadBytes u32, reserved u32 zéro ; tous les entiers little-endian, plafond total 1024 octets. Types fermés `BindRequest`, `BindReply`, `DataOffer`, `DataBindRequest`, `DataBindReply`. Le payload de liaison encode lease/generation u64, AuthenticationId u64, SessionId u32, longueurs u16 des deux SID binaires (maximum 68 chacun), puis SID canoniques et nonce de pipe de 16 octets. Bootstrap : nonce nul ; data : nonce de l'offre. `DataOffer` contient uniquement lease, generation et nonce. Tailles exactes par type, aucun cast de structure wire ; refus des types/champs/réservés inattendus.

Le nom data est construit localement à partir du préfixe fixe `\\.\pipe\Timox.NativeAudio.v1.data.` et des 32 chiffres hexadécimaux du nonce CNG ; aucun chemin arbitraire reçu. Le nom n'est pas un secret. Le serveur garde le même `ConnectionId` de `BrokerSession` : le data pipe n'appelle pas une seconde fois `AttachAuthenticated`. Nouveau TLS et nouvelle observation du jeton sur data ; liaison vérifiée contre le propriétaire bootstrap, le bail et la génération exacts. Seulement après accord, fermer le bootstrap sans `Disconnect` logique. Échec/expiration avant accord : annuler puis `Disconnect` sur le worker. Une nouvelle connexion n'hérite jamais d'un ancien bail.

## Tâche 1 — Flux pipe réel, cancellation et bornes

**Files:** créer `native-windows-audio/broker/pipe_stream_win32.h/.cpp`, `tests/native_audio_pipe_stream_tests.cpp` ; intégration CMake par son propriétaire après coordination.

**Interface interne :** `PipeStream` possède HANDLE, événement d'annulation et OVERLAPPED ; `ReadSome`/`WriteAll` prennent buffer borné et `INativeAudioAuthenticatedChannel::Deadline`, renvoient `NativeChannelResult`. `RequestCancel` signale l'événement sticky ; le worker annule son OVERLAPPED précis et observe sa completion. Aucun verrou concurrent ne couvre l'attente.

- [ ] RED : vrais pipes de test à nonce, lecture/écriture fractionnée, peer qui ne lit plus, EOF, cancel avant/durant I/O, course completion/cancel ; vérifier aucun accès au buffer après retour.
- [ ] Implémenter byte streams overlapped et accept overlapped. Côté client, ouverture locale avec droits spécifiques et `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION` ; retry borné sur PIPE_BUSY avec attente annulable, sans attente synchrone infinie.
- [ ] GREEN : cible `NativeAudioPipeStreamTests`, puis revue des durées et de la destruction ; parent seul décide le commit.

`CancelIoEx` ne garantit ni annulation immédiate ni completion : ne jamais libérer OVERLAPPED sur le seul retour de cette fonction. Le délai arrête les nouveaux travaux ; la restitution attend la completion OS. Ne pas annoncer une borne temps réel absolue que Windows ne garantit pas. [Contrat Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex).

## Tâche 2 — Schannel sur ce flux, sans nouveau transport abstrait

**Files:** créer `native-windows-audio/broker/schannel_stream_win32.h/.cpp`, `tests/native_audio_schannel_pipe_tests.cpp` ; conserver le prototype autonome comme référence.

**Interface interne :** `SchannelStream` possède `PipeStream` et son contexte SSPI ; credentials et pins sont fournis à la construction et détenus jusqu'à fermeture. Handshake/read/write utilisent le même Deadline, des buffers bornés et aucun accès réseau autre que le pipe fourni.

- [ ] RED : duplex sur vrais pipes, handshake fragmenté à chaque frontière, plusieurs records lus ensemble, message audio de 32880 octets réparti sur plusieurs records, tamper, mauvais pin, absence certificat client, refus EKU/validité au cours du vrai handshake, EOF et timeout.
- [ ] Implémenter à partir des appels éprouvés du prototype, sans copier ses vecteurs non comptabilisés ni sa boucle de handshake mémoire. Traiter `SEC_E_INCOMPLETE_MESSAGE` et `SECBUFFER_EXTRA`, découper selon `SECPKG_ATTR_STREAM_SIZES`, valider protocole/AEAD/confidentialité. Renégociation ou état SSPI inattendu : fermeture explicite pour v1.
- [ ] GREEN : `NativeAudioSchannelPipeTests` ; journaliser uniquement résultats et codes, jamais clés, certificats privés ou PCM. Revue puis commit parent.

Le prototype prouve TLS 1.2 ECDHE/ECDSA AES-GCM en mémoire, pas le framing pipe. L'exécution avec clés temporaires utilisateur exige un mode explicite autorisé ; aucun fallback automatique depuis l'échec des clés éphémères. Un SKIP n'est pas une validation TLS. [DecryptMessage et états](https://learn.microsoft.com/en-us/windows/win32/secauthn/decryptmessage--schannel).

## Tâche 3 — Identité Windows et client concret

**Framing vérifié le 24 octobre :** `channel_frames.h` fournit les lectures
bornées audio/TNAC et l'écriture TNAC directement sur SchannelStream. Aucun buffer
audio supplémentaire ni pointeur retenu. Lecture exacte de l'en-tête, validation
de longueur avant payload, aucun octet consommé du message suivant. Le délai
absolu est conservé à chaque fragment. Tout échec ferme le flux ; une trame audio
partielle est effacée et les sorties de liaison restent inchangées. La sémantique
SESSION, l'admission et les comparaisons de bail restent à appliquer par le canal.

Test rouge observé puis cible `/W4 /WX` verte ; sept CTests ciblés réussis.
Le test Schannel réel a aussi été étendu : deux TNAC dans un record TLS, aller/
retour et enveloppe audio maximale à travers plusieurs records, relayés par
fragments ciphertext de 17 octets. Exécution explicite `--temporary-user-key`
réussie : trois clés créées, supprimées et absence vérifiée, aucune confiance
installée. Ce test de framing ne valide pas le contenu PCM ni le futur serveur.

**Point mémoire identifié avant le client concret (historique, résolu ci-dessous) :** chaque SchannelStream
retient actuellement 133120 octets de tableaux. En garder deux pendant le
transfert dépasse déjà les 256 KiB (266240 octets), avant les buffers du canal,
ceux de l'adaptateur ou le token SSPI temporaire. Prévoir un cycle de vie de
buffers partagé/libéré entre bootstrap et data sans perdre les restes TLS,
sans course avec RequestCancel et sans abandonner prématurément le bail.
Ne pas prétendre le plafond tenu par une simple somme des buffers de framing.

**Correction mémoire du 24 octobre :** les buffers de chaque flux sont désormais
32768 octets chiffrés, 16384 en clair et 18437 en sortie. Le profil TLS 1.2 garde
la limite de 16 KiB par record plaintext et couvre 18 KiB de ciphertext plus
l'en-tête de cinq octets ([RFC 5246, section 6.2](https://www.rfc-editor.org/rfc/rfc5246.html#section-6.2)).
Le plafond de handshake accepté est explicitement 32 KiB : les certificats/chaînes
provisionnés devront tenir ce profil. Aucune réduction du contrôle de pin, EKU,
validité ou chiffrement ; pas d'agrandissement dynamique ou de partage de scratch.

Test rouge observé avec les anciens tableaux, puis GREEN : deux `sizeof(Impl)`
et objets Schannel, un token handshake maximal, les deux buffers de l'adaptateur
et 16 KiB de réserve donnent **250400 octets** sur la build x64 courante.
La réserve doit encore être remplacée/vérifiée par les tailles du canal concret
et de ses objets/scratch lors du raccord. Les allocations internes OS/Schannel,
y compris un token surdimensionné rejeté après retour SSPI, restent opaques et
ne sont pas présentées comme bornées par ce total applicatif.
`NativeAudioSchannelAccountingTests` vérifie sans clé ce budget et la conservation
des octets complétés lors d'un timeout. Essai TLS réel réussi : premier flux
gardant deux octets plaintext en attente pendant le handshake du second, puis
lecture intacte ; régressions de framing maximal, fragmentation, pins/EKU et
tamper réussies. Trois clés utilisateur temporaires créées/supprimées, absence
vérifiée. Le bail et le canal concret restent à raccorder et à qualifier.

**Avancement du 24 octobre 2026 — codec et observation OS :**
`channel_binding.h/.cpp` fournit les cinq trames TNAC v1, avec stockage fixe,
longueurs exactes, SID binaires canoniques et comparaisons de tous les champs
de liaison. Le logon SID doit être de forme S-1-5-5-X-Y. Bootstrap : bail,
génération et nonce nuls ; data : tous présents. DataOffer ne contient aucune
identité. Le protocole ne confère aucune confiance aux identités décodées.
`pipe_identity_win32.h/.cpp` capture le jeton du client après lecture du pipe,
revient immédiatement à l'identité serveur puis extrait SID utilisateur/logon,
AuthenticationId et SessionId. Une impersonation préexistante est refusée sans
la remplacer ; un échec de RevertToSelf termine le processus, conformément au
[contrat Windows](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-reverttoself).

Tests rouges observés séparément (encode absent puis observation OS absente),
puis builds Release /W4 /WX réussis. Six CTests passent : ChannelBinding,
PipeIdentity, PipeStream, BrokerTransport, Broker et AccessPolicy. Le codec
couvre les cinq types, tous leurs préfixes tronqués, suffixes, champs invalides,
SID maximal, altération de chaque octet et réutilisation de SessionId avec un
autre LUID. Le test OS utilise un vrai pipe sous le logon courant et vérifie le
retour d'identité et le refus d'une impersonation imbriquée. Aucune clé créée,
aucun pilote installé ; pas de preuve inter-logons ou audio. Le chemin fatal
d'échec de RevertToSelf n'a pas été injecté dans ce test.
Revue indépendante statique favorable ; limite également signalée pour l'échec
d'OpenThreadToken après impersonation, non injecté. Le code effectue le revert
avant de renvoyer cet échec ; aucune qualification de relais inter-logons.

**Ruling de séquencement :** l'API reste HTTP/WS ; une migration partielle du
bearer serait insuffisante. Poursuivre le client concret et le serveur prévus
ci-dessous avant activation native. Ce sous-lot ne réalise pas encore la machine
d'états BootstrapBound/LeasePending/DataBound, la liaison aux pins TLS, la
comparaison avec le certificat provisionné ou le transfert du bail. Ne pas
cocher la tâche 3 complète.

**Files:** créer `native-windows-audio/broker/channel_binding.h/.cpp`, `native-windows-audio/broker/pipe_identity_win32.h/.cpp`, `include/native_audio_authenticated_channel_win32.h`, `src/native_audio_authenticated_channel_win32.cpp`, `tests/native_audio_channel_binding_tests.cpp`.

**Interface publique :** `CreateWindowsNativeAudioAuthenticatedChannel(const NativeAudioChannelCredentials&) -> std::unique_ptr<INativeAudioAuthenticatedChannel>`. La configuration contient uniquement le certificat client déjà provisionné, pin serveur attendu et endpoint local fixe ; la factory duplique/détient les références de certificat. Pas de création de clé, découverte non authentifiée ou confiance automatique. La définition Win32 reste dans ce header dédié.

- [ ] RED : codec de liaison borné, SID invalides, SessionId réutilisé avec autre AuthenticationId, mauvais nonce/bail/génération, réponse d'un autre logon ; test token du vrai pipe sous l'utilisateur courant et comparaison avec le token du processus.
- [ ] Implémenter collecte/revert et machine d'états `Disconnected → BootstrapBound → LeasePending → DataBound → Closed`. Exchange autorise SESSION au bootstrap, puis audio seulement sur data ; ConfirmLease ne réussit qu'après nouvelle authentification et liaison. Cancel sticky fonctionne dans chaque état.
- [ ] GREEN : `NativeAudioChannelBindingTests` et régression `NativeAudioBrokerTransportTests` ; revue puis commit parent.

La DACL client n'accorde pas GENERIC_WRITE/FILE_CREATE_PIPE_INSTANCE, WRITE_DAC ni WRITE_OWNER. L'ACE data vise le SID de logon ; bootstrap vise le compte provisionné. Le premier serveur utilise FIRST_PIPE_INSTANCE et tous refusent les clients distants. [Droits des pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights), [identification du client](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-impersonatenamedpipeclient).

## Tâche 4 — Serveur duplex réutilisant BrokerSession

**Files:** créer `native-windows-audio/broker/pipe_server_win32.h/.cpp`, `tests/native_audio_broker_pipe_tests.cpp`. Aucun SCM dans ces fichiers.

**Interface :** `BrokerPipeServer` possède les connexions pipe/TLS et reçoit un unique `BrokerSession&`, les credentials serveur et une politique fournie par l'hôte. Un worker sérialise Attach/Process/UpdatePolicy/Disconnect ; le contrôle peut demander RequestCancel pour débloquer le transport avant retrait. Le futur service fournit la politique OS et la révocation, pas les tests.

- [ ] RED : connecter le vrai client via `CreateBrokerNativeAudioTransport`, serveur pipe et vrai `BrokerSession` avec transport natif fake ; PCM duplex identique, Full avec nouvel ID/même N0, deuxième propriétaire refusé, client qui disparaît après SESSION, échange data avant Bind refusé, annulation Open/Read/Write, rejet des messages bootstrap PCM.
- [ ] Implémenter le transfert de connexion logique décrit plus haut, une admission globale, quotas et deadlines. Déconnexion/révocation annule les I/O et le transport, attend le worker, supprime toute réponse non envoyée, puis retire le propriétaire avant nouvelle ouverture. Ne jamais bloquer le worker global en attendant qu'un client lent finisse de recevoir une réponse.
- [ ] GREEN : `NativeAudioBrokerPipeTests`, les trois suites production existantes et les suites précédentes. Tests OS courant sans VM/pilote ; tests inter-logons réels restent un gate séparé. Revue puis commit parent.

## Dépendances et critères de sortie

Le provisioning demeure responsable des certificats, des pins/associations protégés HKLM et des ACL CNG machine service/SYSTEM. Le service LocalService doit encore démontrer l'accès à sa clé, sa DACL pipe réelle et la collecte fiable de l'identité console sans privilège LocalSystem implicite. L'hôte de test fournit explicitement des credentials de laboratoire et une politique synthétique ; cette injection ne sera pas le collecteur de production.

Commandes à intégrer après coordination CMake : `cmake --build <build-isole> --config Release --target NativeAudioPipeStreamTests NativeAudioSchannelPipeTests NativeAudioChannelBindingTests NativeAudioBrokerPipeTests`, puis `ctest --test-dir <build-isole> -C Release --output-on-failure -R "NativeAudio(PipeStream|SchannelPipe|ChannelBinding|BrokerPipe|BrokerTransport|Broker|AccessPolicy)Tests"`. Confirmer racine et git status avant build. Toute exécution créant des clés doit être séparée, explicite et documenter leur suppression ; elle n'est pas autorisée par la rédaction de ce plan.

Sortie attendue : preuves de framing, crypto duplex, identité du logon courant, retrait et propriétaire unique sur de vrais pipes, sans driver. Aucun résultat de ce lot ne qualifie l'isolation PCM inter-logons, l'installation service, l'API de contrôle, la latence ou la fidélité audio sur matériel. Ces gates restent ceux du plan N4/N5/N6.
