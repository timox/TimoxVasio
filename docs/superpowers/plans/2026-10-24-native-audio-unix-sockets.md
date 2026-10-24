# Migration du canal local vers les sockets Unix

Décision du 24 octobre 2026 : l'utilisateur demande des sockets aussi en local,
puis accepte la proposition AF_UNIX. Cette décision remplace le choix des pipes
dans les plans du 22 octobre ; elle ne réduit pas l'objectif global PipeWire
Windows, ni l'autonomie de TimoxVasio.dll, ni la compatibilité réseau PWAR.

## Architecture et frontières

- Canal local moteur/broker : AF_UNIX SOCK_STREAM, chemin local protégé, sans
  fallback réseau ni pipe implicite. Le serveur reste borné à quatre clients.
- API graphique : HTTP/WebSocket existants, authentification à terminer.
- Windows/Linux : protocole PWAR existant. AF_UNIX local ne traverse pas le réseau.
- Réutiliser le dispatcher, les codecs, les limites et les tests PCM existants.
  Aucune opération socket bloquante dans un callback audio.
- Ne pas revendiquer un gain de performances avant mesures comparatives : CPU,
  latence, jitter, xruns, coût des copies et du chiffrement sous charge.

## Travail vérifié

Un premier essai .NET a établi bind/listen/connect/accept et transfert exact sur
la machine Windows, sans WSL. Le transport C++ UnixSocketStream ajoute ensuite
les opérations non bloquantes et les délais absolus. RequestCancel est atomique,
sticky ; select vérifie l'annulation au plus toutes les 10 ms hors retards de
planification OS. Aucune borne temps réel n'est revendiquée. Close/destruction
appartiennent au worker et ne doivent pas courir avec une opération.

Le listener reste ouvert lors d'Accept ; l'hôte gère la protection du répertoire
et le retrait de son chemin après arrêt. Le transport ne supprime jamais un
chemin existant pour le reprendre. Aucun stockage de PCM ni buffer emprunté ne
subsiste après le retour d'une opération. WriteAll rapporte le préfixe envoyé
même lors d'une expiration. La suite réelle NativeAudioUnixSocketTests passe,
après un premier échec de build constatant l'absence de l'implémentation.

Revue indépendante statique : aucun défaut important sous le contrat worker
unique. Lecture inverse corrigée pour accepter les lectures courtes ; test
d'écriture sous backpressure ajouté. Le test d'annulation concurrente autorise
une annulation avant entrée : il ne prouve pas une interruption à un point
déterminé de select. Annuler le listener pour interrompre Accept ; annuler la
destination seule ne réveille pas son attente.

## Suite d'implémentation

### Point de décision : identité du logon

L'enquête après a18815a écarte une transposition automatique du PID vers un token
et révèle une dépendance de provisioning à résoudre avant activation :

- WTSQueryUserToken exige LocalSystem et SeTcbPrivilege. Le service LocalService
  prévu n'en dispose pas ; ne pas changer son compte silencieusement pour faire
  passer ce contrôle.
- LsaGetLogonSessionData exige le propriétaire de la session ou un administrateur
  local. Ce n'est pas un collecteur universel pour LocalService.
- Une clé CNG appartenant au SID utilisateur n'établit pas l'isolation entre
  deux logons du même compte : une DACL visant le logon seul ne suffit pas si
  ce compte peut la modifier par ses droits de propriétaire.

Piste à qualifier, pas une autorisation déjà acquise : certificat client unique
par logon, clé non exportable appartenant au service/SYSTEM, droit d'utilisation
limité au SID de logon, association protégée certificat→identité complète→epoch
établie par un composant de confiance. L'identité déclarée dans TLS est comparée
à cette association ; le PID socket reste diagnostique. La provenance initiale
du token, les droits réels du KSP, l'ouverture par le client sans modification de
la DACL et les capacités de LocalService sont des prérequis à démontrer.

Un succès de TLS avec une association injectée dans un banc ne valide ni ce
provisioning ni l'isolation inter-logons. La révocation doit retirer l'association
et annuler les connexions actives : modifier/supprimer la clé seule ne garantit
pas l'arrêt des opérations utilisant des handles déjà ouverts. Microsoft indique
que le KSP peut conserver les clés en cache tant que des handles restent ouverts.

Sources primaires :
[WTSQueryUserToken](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken),
[LsaGetLogonSessionData](https://learn.microsoft.com/windows/win32/api/ntsecapi/nf-ntsecapi-lsagetlogonsessiondata),
[NCryptSetProperty](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptsetproperty),
[cache KSP et NCryptOpenKey](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptopenkey).

### Contrat du prochain sous-lot : SocketSession sur un flux TLS

**Comptabilité mémoire socket (Release x64) :** compteurs compilés dans les
unités définissant les objets privés, sans estimation de leur sizeof depuis
les headers opaques. UnixSocketStream compte objet+Impl. SocketSession compte
ses objets et tableaux, le TLS (sans recompter son membre déjà dans Impl), et
le socket détenu. SocketServer ajoute quatre sessions, listener, socket accept
de réserve et enrollment partagé. Le client compte sa machine d'états, le TLS
et le socket. Les compteurs n'allouent rien et n'ouvrent aucune connexion.

Assertions exécutées via --completion-accounting-only, sans clé créée :

| Périmètre | Enveloppe calculée | Seuil |
|---|---:|---:|
| Client socket + adaptateur | 175848 octets | 262144 |
| Session serveur | 175664 octets | 262144 |
| Serveur quatre sessions | 704280 octets | 1048576 |

Les enveloppes ajoutent le token handshake accepté maximal (32768), une réserve
scratch applicative de 8192 par côté/session, 1024 de métadonnées, et 1024 globaux
supplémentaires pour le serveur. Les deux tableaux adaptateur et son surcoût
borné par static_assert sont comptés côté client. Les seuils sont par extrémité,
pas une somme client+serveur ni une limite de RSS. Sont exclus : surcoût de
l'allocateur, allocations opaques Winsock/Schannel/certificats, piles réservées
des threads, dispatcher/BrokerSession partagés et backend natif. Les réserves
scratch ne sont pas une mesure du high-water mark des piles. La mémoire totale
du service et les performances doivent encore être mesurées sur le système.
Build /W4 /WX des bibliothèques client et serveur sans macro de test réussi.

**Couverture client défensive complétée :** SocketClientBadReply utilise un
serveur Schannel scripté sur AF_UNIX et la factory concrète avec l'adaptateur
audio. Le témoin accepte les réponses valides. Huit altérations sont refusées :
LUID du BindReply, bail/génération DataOffer, nonce/bail/génération/LUID du
DataBindReply, requestId de la réponse SESSION. Open ne rend pas de génération
valide ; réutiliser le canal échoue et efface tout le buffer de réponse.
SocketClientHandshakeCancel reçoit des octets ClientHello avant annulation,
garde le pair ouvert et exige completion Cancelled sous deux secondes, puis
vérifie que Close ne réinitialise pas l'annulation. Ces cas passent sans modifier
le code produit, comblant les limites de revue spécifiques au nouveau client.
Build /W4 /WX et suite TLS complète réussis ; trois clés temporaires supprimées
et absence vérifiée. Ce sont des tests de canal, pas de provisioning ni de
latence audio. Les contrôles d'identité OS et d'isolation demeurent distincts.
Revue des tests : assertions renforcées pour exclure un simple timeout après
arrêt d'envoi du serveur scripté. Les liaisons altérées doivent rendre AccessDenied,
le requestId altéré Failed/ERROR_INVALID_DATA. Le pair constate la fermeture du
socket ; une requête STATUS valide sur le canal fermé est ensuite refusée avec
ERROR_INVALID_PARAMETER et buffer effacé. Ne pas attribuer cette fermeture au
seul client bas niveau : l'adaptateur appelle aussi Close en cas d'échec d'Open.

**Supervision native ajoutée :** le délai du stream seul n'interrompait pas
broker.Process bloqué dans le backend. Régression reproduite avec un Open
simulé annulable : client TLS maintenu ouvert, Open atteint, toujours bloqué
après sept secondes malgré l'échéance setup de cinq secondes. Après correction,
SocketSession publie l'échéance existante avant chaque Process et l'efface à la
completion ; CheckDeadline annule TLS et dispatcher si expirée. Publication,
effacement, retrait et décision d'annulation partagent le mutex de session pour
éviter une annulation tardive d'une opération remplaçante.

SocketServer appelle CheckDeadline pendant ses boucles accept et quota, toutes
les 200 ms au plus hors scheduling/traitements. Une instance SocketSession
utilisée seule doit recevoir ces appels d'un superviseur. Aucune modification
de l'échéance initiale ou relance automatique. Le backend doit toujours honorer
RequestCancel : pas de prétendue borne de completion temps réel en cas contraire.

Tests réels AF_UNIX/TLS avec backend bloquant : Open, ReadRender et WriteCapture
atteints puis annulés sans fermeture client ni expiration de lecture côté client.
Aucune réponse expirée acceptée, session retirée. Bibliothèque serveur /W4 /WX,
dix CTests et suite crypto réussis ; trois clés temporaires supprimées et absence
vérifiée, y compris lors de la reproduction rouge. Revue indépendante sans
défaut bloquant ; aucune qualification d'un pilote ou périphérique physique.

**Écoute autonome du 24 octobre :** SocketServer possède un listener et quatre
slots fixes session/thread. Run appartient à un acceptor ; RequestCancel et
ActiveConnections sont concurrents, tous les appels doivent finir avant
destruction. Les workers publient leur fin sous mutex, le thread est joint hors
mutex avant de détruire la session. Une exception d'allocation/lancement passe
par Cancel puis drainage. Listen n'efface aucun chemin ; ACL, découverte et
retrait du chemin restent à la charge de l'hôte de confiance.

Le certificat serveur est dupliqué et détenu par RAII. L'enrollment révoqué est
détecté durant les attentes accept/quota (poll de 200 ms, sans borne de scheduling
OS), puis toutes les sessions sont annulées et jointes. Une cinquième connexion
peut rester dans le backlog OS ; elle n'obtient pas de cinquième worker.

RED initial : header serveur absent. GREEN : test du client concret désormais
sans accept/threads de sessions manuels, PCM, OwnedElsewhere et six réouvertures
successives ; test quota quatre sockets muettes au handshake, révocation avec
clients ouverts, Run terminé en moins de deux secondes et zéro session active.
Ce test ne prouve pas l'arrêt d'un backend natif qui ignorerait RequestCancel.
Revue indépendante : aucun défaut bloquant ; restent sans injection directe
annulation avant Run, révocation listener inactif et échec de création de thread.
Bibliothèque NativeAudioWindowsBroker compilée /W4 /WX sans macro de test,
dix CTests ciblés réussis et banc TLS réel réussi ; trois clés temporaires
supprimées et absence vérifiée. Pas d'installation SCM ni activation native.

**Client concret du 24 octobre :** `src/native_audio_socket_channel_win32.cpp`
implémente INativeAudioAuthenticatedChannel avec un seul socket/TLS et états
Disconnected→BootstrapBound→LeasePending→DataBound→Closed. La factory reçoit
des credentials déjà provisionnés et un chemin venant de l'hôte de confiance ;
pas de découverte ni de fallback. Le client relève son propre token, exige son
identité exacte dans la réponse, valide chaque requête/réponse et confirme le
bail/génération/nonce sur le même flux avant PCM. Cancel sticky, pointeurs fixes,
erreurs terminales avec effacement de la sortie bornée. Aucun changement du
protocole PWAR ou de TimoxVasio.dll.

RED observé : factory absente ; build client/socket dans NativeAudioWindowsChannel
sans macro test et banc TLS réussis. ConcreteSocketClient utilise l'adaptateur
réel face au gestionnaire et au dispatcher : PCM maximal identique aller/retour,
Full puis retry, STATUS=12/34/56, CLOSE et aucun propriétaire restant. Deuxième
connexion TLS au même dispatcher : OwnedElsewhere, puis PCM du premier continue.
Dix CTests ciblés et suite crypto réelle passent ; trois clés temporaires
supprimées et absence vérifiée. Ce scénario utilise un enrollment et une politique
synthétiques, pas un provisioner de production ni une écoute autonome à quatre
workers. Les enveloppes mémoire calculées précédemment concernent encore le
client pipe ; budget socket complet à mesurer.
Revue indépendante sans défaut bloquant. Couverture à compléter explicitement
sur ce nouveau client : serveur répondant avec binding/offre altérés et annulation
pendant son handshake. Les tests de l'ancien client pipe et ceux des refus côté
SocketSession ne remplacent pas ces scénarios client socket.

**Implémentation du 24 octobre :** socket_session_win32.h/.cpp raccordé au
dispatcher partagé. Enrollment immuable pin/identité/epoch avec révocation
terminale. Le propriétaire de l'enrollment doit Revoke puis RequestCancel toutes
ses sessions avant changement de politique ; epoch n'est pas un registre global
et sa rotation n'est pas automatiquement orchestrée par ce gestionnaire.
Handshake/liaison/SESSION/confirmation partagent une échéance absolue de cinq
secondes ; chaque échange audio possède sa propre échéance. Une opération native
bloquée dans le dispatcher requiert l'annulation du superviseur hôte.

RED initial : classe absente ; build /W4 /WX puis neuf scénarios SocketBroker
sur sockets TLS réelles réussis, dans le banc Schannel existant. PCM 4096 frames
comparé intégralement dans les deux sens, capture Full puis retry, fermeture et
zéro propriétaire à chaque sortie. Rejets identité/bail/génération/nonce altérés,
PCM avant SESSION/confirmation, EOF après SESSION, révocation après l'offre.
Revue indépendante sans défaut bloquant. Son constat sur le test de révocation
a été traité : assertion de terminaison Cancelled sous deux secondes avec client
encore ouvert ; omission volontaire de revoke/cancel → échec observé, remise en
place → suite TLS verte. Nettoyage des trois clés vérifié même sur l'essai rouge.
Dix CTests ciblés passent. Pas de preuve nouvelle inter-logons ni de performances.

Le test utilise un client filaire de banc et un backend PCM simulé, pas encore
le client moteur concret socket. Listener borné, connexion concurrente d'un
second propriétaire sur TLS, cancellation pendant backend bloqué, comptabilité
mémoire du nouveau gestionnaire et provisioning OS restent à terminer.

Revue indépendante du 24 octobre : poursuivre le gestionnaire avec un
enregistrement de confiance injecté, sans annoncer une équivalence avec
l'identité physique observée sur pipe. Ce sous-lot permet de raccorder le
protocole et le broker pendant que le provisioning est qualifié.

- Configuration serveur immuable : certificat serveur déjà provisionné,
  empreinte client attendue, ChannelIdentity enregistrée et epoch non nul.
  Aucun enrôlement reçu du client ni lookup par un PID non fixé.
- Un seul flux AF_UNIX/TLS par connexion logique. Le dispatcher est partagé ;
  le gestionnaire n'en crée jamais un second. Limite d'admission : quatre.
- États : TLS authentifié → BindRequest/BindReply comparés à l'enregistrement
  → SESSION → confirmation explicite du bail et de la génération → PCM.
  Pas de nouveau pipe/socket de données ; pas d'accès PCM avant confirmation.
- Toute sortie retire l'identifiant du dispatcher ; annulation sticky de la
  socket et du backend, réponses en attente effacées. Révocation d'epoch retire
  les enregistrements et ferme les flux actifs ; pas seulement leurs clés.
- Tests réels avec enrollment explicitement synthétique : PCM duplex complet,
  Full puis retry, deuxième propriétaire refusé, EOF après SESSION, mauvaise
  identité déclarée, génération/bail altérés, PCM prématuré, délais et annulation.
  Les qualifications de production restent séparées et l'activation demeure off.

**Limite décisive de la revue :** TLS avec enrollment authentifie un détenteur
de credential, pas nécessairement le processus connecté physiquement. Un relais
transparent ne lit pas le PCM chiffré, mais n'est pas détecté par cette seule
méthode. La promesse historique de refus de tout relais inter-logons reste non
démontrée ; ne pas la considérer satisfaite en changeant simplement la définition.
Une clé non exportable n'empêche pas un oracle de signature, ni à elle seule
l'accès d'un autre logon du même compte aux processus/tokens/handles autorisés.
Ces cas exigent une qualification OS réelle. Aucun basculement à LocalSystem,
création de clé machine ou installation de composant privilégié dans ce sous-lot.

**Observation du pair :** le SDK partagé `afunix.h` définit
SIO_AF_UNIX_GETPEERPID, renvoyant un ULONG. GetPeerProcessId l'utilise uniquement
sur une socket connectée et conserve la sortie lors d'un échec. Le test réel
vérifie le PID courant puis démarre un processus enfant sans fenêtre et contrôle
que le serveur voit son PID exact et que l'enfant voit celui du parent. Les
processus et sockets restent vivants pendant ces assertions. Cela exclut un
simple retour accidentel de GetCurrentProcessId, mais ne qualifie pas l'identité
de sécurité. Windows 10.0.26200 retourne le PID valide avec bytes=0 ; l'hypothèse
initiale bytes==4 a échoué, puis le contrôle 0-ou-4/non-zéro passe.

**Limite d'autorisation :** un PID ne conserve pas le processus vivant. Ouvrir
son token après cette lecture peut rencontrer un PID réutilisé ; un handle de
socket transféré ou un relais ne prouve pas l'identité du détenteur actuel. Ne
pas remplacer directement ObservePipeIdentity par PID→OpenProcessToken. Le
contrat d'authentification reste à définir avec preuves contre ces scénarios,
et accès LocalService à vérifier. La connexion n'est pas encore autorisée au
broker sur la base de cette observation.

**Avancement TLS du 24 octobre :** ByteStream fournit ReadSome/WriteAll/Close/
RequestCancel, destruction virtuelle, limites et délais communs. SchannelStream
en possède une instance ; PipeStream et UnixSocketStream l'implémentent. Le test
UnixTls a d'abord échoué à compiler parce que Schannel exigeait PipeStream,
puis passe sur AF_UNIX réel : 32 Kio aller-retour comparés intégralement, mauvaise
empreinte serveur refusée. La bibliothèque client historique compile également.
Dix CTests ciblés passent et essai crypto explicite réussi ; trois clés utilisateur
temporaires créées/supprimées/absence vérifiée. Enveloppe client pipe : 243920
octets après ajout du polymorphisme (pas une mesure du futur client socket).
Le prototype pipe_server non commité a été supprimé avec ses deux scénarios ;
les tests pipe historiques restent une régression. Aucune preuve d'identité
Windows ou de liaison au broker ne découle de ce test TLS socket.

1. Séparer SchannelStream du type PipeStream concret ; brancher UnixSocketStream
   via une interface de flux minimaliste et vérifier TLS duplex sur AF_UNIX.
2. Définir et tester la découverte du chemin et ses ACL, le provisioning et la
   liaison de l'identité au canal. Ne pas transposer ImpersonateNamedPipeClient
   à une socket, ni croire un SID/PID envoyé par le client. Un certificat valide
   ne prouve pas à lui seul le logon courant : ce contrat doit être résolu avant
   d'activer le broker natif. Aucun achat de certificat commercial supposé.
3. Adapter client et serveur ensemble ; déterminer si le transfert bootstrap/data
   reste nécessaire avec la nouvelle liaison. Préserver l'arbitrage unique et
   le retrait du bail sur EOF, annulation et révocation. Retirer le prototype
   pipe_server non commité, désormais abandonné.
4. Tester clients simultanés, refus d'un second propriétaire, révocation pendant
   Open/Read/Write et réponse préparée, échec d'authentification, chemin usurpé,
   tailles bornées et budget mémoire. Pas de preuve inter-logons par une simple
   politique synthétique ; garder la qualification réelle comme gate distinct.
5. Raccorder au service et au moteur, puis qualifier les endpoints et les
   performances. Aucune activation native actuelle ne découle du test socket.

Références : [AF_UNIX Windows](https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/),
[select Winsock](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select),
[connect non bloquant](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect).
