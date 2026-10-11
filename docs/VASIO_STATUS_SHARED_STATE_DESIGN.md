# `vasio-status` — état partagé proposé

## État actuel (constaté dans le code)

`AudioClientMapping` (`include/audio_transport.h`, `src/audio_transport.cpp`) crée **une zone par processus client et par pilote** : `Local\VASIO-Map-<PID>-<driverId>` et un événement `Local\VASIO-Event-<PID>-<driverId>`. Le client crée la zone ; le moteur l'ouvre après détection du processus. Le format actuel porte `magic=VSAT`, `version=6`, PID, fréquence, taille de bloc, deux masques de 256 voies, l'état d'attachement du moteur, quatre compteurs d'underrun/overrun et deux anneaux de 16 384 trames × 256 voies en `float` (client→moteur et moteur→client). Les index de lecture/écriture et les compteurs utilisent `Interlocked*`. Le masque des voies utilise une génération paire/impaire pour lire un instantané cohérent.

Le moteur découvre les clients dans `VasioClientManager` : il cherche les zones des PID vivants, puis vérifie le module `TimoxVasio.dll`. Il écrit la fréquence et le bloc confirmés, et signale les clients via l'événement. Le callback du pilote lit son entrée, appelle l'hôte ASIO, puis écrit sa sortie. Les routes et la présence d'un écrivain par voie sont connues du **moteur**, pas de la DLL.

La DLL compilée aujourd'hui n'inventorie pas les périphériques, n'ouvre pas d'autre pilote ASIO et n'écrit pas de journal. Elle lit toutefois le fichier des profils d'application lors de la création de l'objet ASIO, pour décider du nombre de voies annoncées ; ce comportement préexistant n'est pas un diagnostic. Ses messages d'erreur ASIO et les compteurs du transport servent au fonctionnement et à la détection des pertes.

## Structure à ajouter, avant implémentation

Ajouter un bloc `ClientStatusV1` **à la fin** de chaque zone client, après les anneaux audio. Le bloc appartient au client ; le moteur et `vasio-status` le lisent seulement. La zone passe à une version de format 7 et porte `statusSize` et `statusVersion`, afin qu'un lecteur refuse proprement un format inconnu. Les zones v6 restent lisibles par le moteur pendant la transition, mais `vasio-status` les étiquette « ancien pilote : état détaillé indisponible ». Prévoir un nom de zone v7 distinct si les versions 6 et 7 doivent coexister pour un même PID lors d'une mise à jour.

| Groupe | Champs proposés | Producteur / lecture |
| --- | --- | --- |
| En-tête | signature `VSST`, version de schéma, taille, indicateur `ready`, fréquence QPC | DLL à l'initialisation ; outil en lecture seule |
| Identité | version DLL à 4 nombres, chemin absolu de **sa propre** DLL, PID | DLL une fois à l'initialisation ; nom du processus résolu par l'outil à partir du PID |
| Client | état `initialized / buffers-ready / running / stopped / detached`, dernier échange QPC | DLL lors des transitions et callbacks ; masques des voies déjà présents dans la zone v6/v7 |
| Format | fréquence et taille demandées par ce client | DLL lors de `setSampleRate` et `createBuffers` ; fréquence et bloc confirmés déjà écrits par le moteur |
| Horloge | nombre de callbacks, trames échangées, QPC du premier et du dernier callback, dernier intervalle QPC | DLL : incréments et horodatages atomiques ; l'outil calcule période moyenne, période théorique et dérive ppm |
| Erreurs | compteurs de demandes de réinitialisation émises, dernier code d'erreur ASIO | DLL si ces événements se produisent ; les quatre compteurs d'underrun/overrun existent déjà dans le transport |

Les chaînes d'identité sont écrites **avant** la publication atomique de `ready`. Les compteurs et horodatages sont alignés sur 64 bits et publiés sans allocation, sans E/S et sans verrou bloquant dans le callback. Si plusieurs champs doivent former un instantané cohérent, le client emploie une génération paire/impaire à écrivain unique ; l'outil relit cette génération et marque l'instantané « instable » après un nombre borné de tentatives. Le lecteur ouvre la zone avec `FILE_MAP_READ`, jamais `FILE_MAP_ALL_ACCESS`. L'implémentation doit vérifier que les lectures atomiques alignées fonctionnent sur une vue réellement en lecture seule en x64.

`vasio-status` complète l'état local sans demander à la DLL de faire un inventaire : nom et vie du processus via Windows, route et écrivain de chaque voie via l'API du moteur, chemin/version du pilote via le bloc. Une voie lue sans écrivain est une **analyse de route**. Un client fantôme est un PID absent ou un client qui disparaît de la liste alors qu'il était `running` dans l'historique de l'outil ; la DLL ne peut plus publier après la mort brutale du processus. Les compteurs de réinitialisation restent « non pris en charge » tant qu'aucun chemin du pilote n'émet de demande ASIO : afficher zéro comme preuve de santé serait trompeur.

L'outil calcule les écarts de fréquence et de buffer entre demandes et format confirmé, la période observée à partir de QPC, ainsi que les erreurs qui **augmentent** entre deux relevés. `--json` produit un instantané machine ; l'historique est enregistré par l'outil uniquement sur demande, jamais par la DLL. Le code couleur rouge signale les incohérences sur la sortie console, sans modifier les zones ni le système.

## Décisions à valider avant code du bloc

1. Format v7 append-only et coexistence temporaire avec v6 dans le moteur.
2. Chemin DLL et version publiés par chaque instance à l'initialisation ; nom du processus et analyse des routes enrichis par l'outil.
3. Les erreurs de routage et les clients disparus sont **inférés hors DLL**, car ils ne relèvent pas de son état propre.

## État de l'implémentation (7 octobre 2026)

La structure v7 approuvée est dans `src/audio_transport_layout.h`. La DLL
publie son propre chemin, sa version, son état, les formats demandés et les
compteurs QPC. Le callback n'effectue ni allocation, ni E/S disque, ni
inventaire. Les champs d'échange utilisent une génération paire/impaire ; le
lecteur borne les tentatives et indique si l'instantané reste instable.

Le lecteur `src/audio_transport_status_reader.cpp` est lié uniquement aux
outils et au test `AudioTransportProbe`, **pas à `TimoxVasio.dll`**. Il ouvre
la zone avec `FILE_MAP_READ`. `vasio-status` résout les processus et les
routes actives par l'API du moteur ; ces routes utilisent le PID dans leurs
identifiants. Le contrôle des voies sans écrivain porte sur les voies d'entrée
activées dans la zone partagée. Le client fantôme est détecté si l'observateur
a vu disparaître un client `running`, ou si le moteur liste encore un PID
absent de l'inventaire des processus Windows au premier relevé.

La DLL v7 de test émet la notification ASIO minimale de réinitialisation
autorisée lorsque le format confirmé change pendant que ses anciens buffers
tournent. `vasio-status` publie alors `resetRequestsSupported: true` et le
nombre de demandes émises. Une zone v6 reste lisible comme `legacy`, avec
`resetRequestsSupported: false` : son zéro ne prouve aucune absence de demande.
Le 10 octobre 2026, la DLL de test est enregistrée depuis
`C:\Program Files\Steinberg\VirtualASIO\1.1.1-test.1\TimoxVasio.dll`
(SHA-256 `B9F0F6D074625C51F23D0C5300DA6C1ADE443725FC538B214B30B4E2FDCD8FBB`).
Cette installation de test ne constitue pas une publication 1.1.2.

Le schéma interne du bloc d'état est passé à `ClientStatusV2` (version de
schéma 2, dans la zone de transport v7). Il sépare l'horodatage du dernier
callback de celui du dernier échange audio réussi : un callback dont
l'écriture a échoué incrémente le nombre de callbacks, mais ne rafraîchit
pas le « dernier échange » affiché par `vasio-status`.
Le compteur `successfulExchangeCount` dénombre les buffers effectivement
écrits ; `callbackCount` inclut aussi les callbacks dont l'écriture a échoué.
