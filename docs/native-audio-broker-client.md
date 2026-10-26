# Transport client du broker natif

Le client `CreateBrokerNativeAudioTransport` adapte le protocole du broker à
`INativeAudioTransport`. Il est destiné au worker du moteur, jamais au callback
audio. Il ne modifie pas le pilote ASIO autonome.

L'ouverture exige successivement un canal authentifié, une réponse SESSION
validée et une confirmation du bail sur le canal de données. La génération
n'est publiée au moteur qu'après ces trois étapes. Sans canal fourni, le client
retourne `Unavailable` ; il ne sélectionne pas le transport direct de laboratoire.

Deux buffers fixes bornent les messages. Chaque réponse est contrôlée pour
l'opération, l'identifiant de requête, le bail, la génération et la taille avant
copie du PCM. Une réponse `Full` permet de réessayer avec un nouvel identifiant ;
les erreurs terminales ferment le canal. L'annulation est persistante et peut
venir d'un autre thread, tandis que fermeture et échanges restent sur le worker.

## Vérification du 22 octobre 2026

Build Release sous MSVC avec `/W4 /WX` réussi. Les trois suites de
`build_native_production` passent : politique d'accès, session broker, transport
client. Le banc client échange avec le véritable `BrokerSession`, au travers
d'un canal en mémoire et d'un périphérique simulé. La revue indépendante n'a
relevé aucun défaut bloquant.

Les essais couvrent notamment le duplex PCM, le second propriétaire refusé,
la fermeture/réouverture, la confirmation de bail refusée, les réponses altérées,
les bornes de taille, les reprises après saturation et l'annulation aux trois
phases d'ouverture/échange.

## Ce qui reste à raccorder

`INativeAudioAuthenticatedChannel` est un contrat, pas encore un canal Windows
implémenté. Authentification réelle, liaison à l'identité Windows, respect des
délais et achèvement des IO annulées restent à vérifier sur ce futur canal.
Le timeout réel et l'épuisement du compteur client ne sont pas exercés par ces
tests. Le moteur distribué n'active pas encore ce transport.

Ces résultats ne prouvent ni l'isolation audio entre logons, ni le fonctionnement
du miniport en noyau, ni les performances de bout en bout. La qualification VM
reste décrite dans [le protocole de qualification](native-audio-session-qualification.md).

## Flux Windows local

`PipeStream` fournit maintenant un flux byte-mode sur de vrais pipes Windows,
avec IO overlapped, annulation persistante et attente de completion avant
restitution des buffers. Le délai déclenche l'annulation ; le drainage de la
completion Windows peut dépasser ce délai. Ce flux seul n'authentifie personne.

La suite `NativeAudioPipeStreamTests` passe avec les trois suites précédentes
(4/4). Elle couvre les transferts fragmentés, EOF, timeout, connexion occupée,
annulation de lecture/écriture/acceptation/connexion et courses entre completion
et annulation. Une revue indépendante n'a relevé aucun défaut bloquant.
Les ACL de production, Schannel sur ce flux et la liaison au logon restent à
intégrer et qualifier ; les tests courants n'activent aucun pilote ni service.

## Prototype Schannel sur pipes réels

Le prototype `SchannelStream` a ensuite passé son test explicite
`NativeAudioSchannelPipeTests --temporary-user-key` : certificats des deux pairs,
pins, validité/EKU, TLS 1.2 AES-GCM, fragmentation et transfert audio simulé.
Les trois clés utilisateur temporaires ont été supprimées et leur absence
vérifiée. CTest sans argument saute ce test pour ne pas créer de clés implicitement.

Une correction conserve les octets chiffrés consommés lorsqu'une lecture termine
malgré son timeout. Une fixture déterministe vérifie cette comptabilisation ;
elle ne prétend pas reproduire le timing de la course Windows. La revue de la
correction est favorable. La liaison à l'identité Windows, les ACL de service et
l'isolation entre logons restent à qualifier. Ce prototype n'est pas activé dans
le moteur et n'est pas le transport réseau PWAR.
