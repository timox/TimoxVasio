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
