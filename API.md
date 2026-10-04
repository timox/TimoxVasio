# API locale de contrôle TimoxVasio v1

Cette API est le contrat entre l’interface de contrôle et le moteur natif. Electron main en est le client ; le renderer ne lit ni le registre, ni le fichier de configuration, ni la mémoire audio partagée.

La définition [Swagger/OpenAPI 3.1](openapi-v1.json) décrit les routes HTTP et référence les schémas JSON partagés. Son extension `x-websocket` décrit les commandes, réponses et événements de la connexion WebSocket.

Le serveur natif utilise `cpp-httplib` 0.58.0 pour HTTP et WebSocket, et `nlohmann/json` 3.12.0 pour encoder et décoder les messages. Les en-têtes et licences de ces dépendances sont conservés dans `vendor/`.

L’inventaire physique énumère les pilotes ASIO enregistrés sans les ouvrir. `capabilitiesKnown` vaut `false` et les capacités sont vides tant qu’un pilote n’a pas été explicitement sélectionné et ouvert par le moteur.

Les messages sont encodés en JSON UTF-8. Les schémas normatifs sont dans [`schemas/api-v1.json`](schemas/api-v1.json). Les unités sont explicites dans les champs : fréquence en hertz, taille de buffer en frames, canal numéroté à partir de 1, gain en dB et niveau de crête en dBFS.

## Adresse et lecture

Le moteur écoute uniquement sur l’interface loopback `127.0.0.1`. Le port peut être demandé à son démarrage ; le port `0` demande un port disponible. Le processus client récupère le port sélectionné dans l’information de démarrage du moteur.

| Requête | Réponse |
|---|---|
| `GET /api/v1/state` | État courant confirmé du moteur, sélection matérielle, configuration TimoxVasio, routes et inventaires |
| `GET /api/v1/drivers` | Inventaire des pilotes matériels ASIO découverts et de l’unique pilote virtuel TimoxVasio |
| `GET /api/v1/application-profiles` | Profils de capacité de canaux ASIO appliqués selon le nom de l’exécutable |
| `PUT /api/v1/application-profiles` | Remplacement complet et persistant des profils de capacité |
| `ws://127.0.0.1:<port>/api/v1/ws` | Commandes de configuration et événements d’état/audio |

Les deux routes HTTP sont appelées sous le même préfixe `http://127.0.0.1:<port>`.

Le déroulé API puis redémarrage d'hôte est schématisé dans
[Séquences du pilote](docs/DRIVER_SEQUENCES.md#2-profil-par-application-via-lapi-et-swaggeropenapi).
Le document [OpenAPI](openapi-v1.json) est la définition utilisée pour
présenter ces routes dans Swagger UI.

Les réponses HTTP sont des objets JSON conformes à `definitions.state` ou `definitions.drivers`. Un pilote matériel contient ses ports d’entrée et de sortie, les taux annoncés par son pilote ainsi que ses capacités de buffer. TimoxVasio expose les clients détectés et seulement les canaux alloués par chacun via `createBuffers`, avec leurs ports actifs.

Forme des identifiants d’extrémités :

```text
physical:<driverId>:input:<channel>
physical:<driverId>:output:<channel>
virtual:TimoxVasio:<client-pid>:input:<channel>
virtual:TimoxVasio:<client-pid>:output:<channel>
```

Le suffixe canal est un entier décimal à partir de 1. Les ID renvoyés par le moteur sont à réutiliser tels quels dans la configuration et les vues.

## Profils de capacité par application

`GET /api/v1/application-profiles` renvoie la liste complète des profils explicitement configurés :

```json
{
  "profiles": [
    { "processName": "mixxx.exe", "inputChannels": 255, "outputChannels": 255 }
  ]
}
```

Le nom doit être un nom de fichier exécutable Windows, sans chemin. La comparaison ne distingue pas les majuscules. Une application absente de la liste reçoit 256 entrées et 256 sorties. Le profil initial de Mixxx est fixé à 255 dans les deux directions pour éviter que sa version stable interprète la capacité 256 comme zéro. La limite s’applique à `getChannels`, `getChannelInfo` et `createBuffers`; elle n’altère pas les 256 slots du transport partagé. Les applications peuvent donc échanger par les canaux communs réellement alloués et les routes de l’API.

`PUT /api/v1/application-profiles` remplace la liste entière :

```json
{
  "profiles": [
    { "processName": "mixxx.exe", "inputChannels": 255, "outputChannels": 255 },
    { "processName": "autre-hote.exe", "inputChannels": 128, "outputChannels": 64 }
  ]
}
```

Les comptes doivent être compris entre 1 et 256 inclusivement. Une liste vide supprime les dérogations et rétablit la capacité par défaut de 256 pour chaque application. La réponse confirme les profils enregistrés et liste dans `restartRequiredClients` les clients déjà connectés dont le nombre annoncé change. Un hôte déjà initialisé conserve sa réponse `getChannels`; il faut le fermer puis le relancer pour prendre son nouveau profil. Les modifications ne redémarrent ni ne déconnectent les applications à la place de l’utilisateur.

Une requête invalide renvoie le statut HTTP 400 et une erreur structurée `INVALID_APPLICATION_PROFILES`. Le stockage est atomique; si l’enregistrement échoue, le profil actif précédent est conservé.

Après l’ouverture explicite d’un pilote physique, le champ `name` de ses endpoints reprend le nom de canal renvoyé par `IASIO::getChannelInfo`. Si le pilote ne fournit pas de nom exploitable, le moteur publie un nom de remplacement composé du nom du pilote, de la direction et du numéro de canal. Avant l’ouverture, les endpoints et capacités physiques sont vides.

## WebSocket

Chaque commande est un message JSON :

```json
{
  "id": "request-42",
  "command": "configuration.apply",
  "payload": {
    "physicalDriverId": "physical-driver-stable-id",
    "sampleRate": 48000,
    "bufferFrames": 256,
    "routes": [
      {
        "id": "route-1",
        "sourceEndpointId": "virtual:TimoxVasio:1234:output:1",
        "destinationEndpointId": "physical:physical-driver-stable-id:output:1",
        "gainDb": 0.0,
        "mute": false
      }
    ]
  }
}
```

`id` est une chaîne non vide de 1 à 128 caractères et doit être unique parmi les commandes en attente sur cette connexion. La commande `configuration.apply` remplace en une opération le pilote physique, la fréquence, la taille de buffer et la liste complète des routes. L’objet complet est validé avant mutation. Répéter exactement la même configuration est idempotent.

`sampleRate` et `bufferFrames` sont les valeurs du pilote physique choisi. `null` demande respectivement le taux courant et la taille préférée annoncés par ce pilote. Le moteur fixe le taux physique, relit ses capacités dépendantes du taux et refuse toute taille qu’il n’annonce pas. TimoxVasio reçoit exactement ces valeurs ; il n’a pas de réglage de fréquence ou de taille indépendant.

Un succès est acquitté avec le même identifiant :

```json
{ "id": "request-42", "success": true, "result": { "accepted": true } }
```

Un refus ou un échec renvoie une erreur structurée :

```json
{
  "id": "request-42",
  "success": false,
  "error": {
    "code": "UNSUPPORTED_SAMPLE_RATE",
    "message": "Le pilote matériel ne prend pas en charge 88200 Hz",
    "operation": "open",
    "driverId": "physical-driver-stable-id",
    "asioError": -998
  }
}
```

Les codes d’erreur sont stables et leur texte peut être localisé :

| Code | Signification |
|---|---|
| `INVALID_CONFIGURATION` | Champs manquants ou configuration mal formée |
| `UNKNOWN_ENDPOINT` | Une extrémité n’existe pas dans l’inventaire courant |
| `INVALID_ROUTE_DIRECTION` | Le lien n’est pas l’un des trois types autorisés |
| `UNSUPPORTED_SAMPLE_RATE` | Le taux n’est pas accepté par le pilote ASIO physique choisi |
| `UNSUPPORTED_BUFFER_SIZE` | Le pilote refuse le nombre de frames demandé |
| `PHYSICAL_DRIVER_OPEN_FAILED` | Le pilote matériel ne peut pas être ouvert ou démarré |
| `AUDIO_ENGINE_UNAVAILABLE` | Le moteur ou le transport d’un client VASIO est indisponible |
| `INTERNAL_ERROR` | Erreur non classée pendant l’application de configuration |

## Interruption de configuration

Un changement effectif arrête le flux avant de détruire ou reconstruire les buffers et le graphe. L’ordre observable est :

1. réponse de commande `accepted` ;
2. événement `engine.status` avec `state: "reconfiguring"` ;
3. événement `engine.status` avec `state: "stopped"` si le pilote physique est configuré mais qu’aucune route n’existe encore ; ses capacités et ses endpoints sont alors publiés par `/api/v1/drivers`, sans démarrer de flux audio ;
4. événement `engine.status` avec `state: "running"` si au moins une route existe et que la validation, l’ouverture du périphérique, la création des buffers et le démarrage réussissent ;
5. sinon, événement `engine.error` puis `engine.status` avec `state: "error"`. Le moteur reste arrêté et ne réactive pas l’ancien graphe.

Une configuration identique à celle qui est déjà active répond `accepted` sans arrêter à nouveau le flux. Une commande invalide est rejetée avant toute interruption. L’interface désactive les mutations pendant `reconfiguring` et affiche l’état confirmé par le moteur.

L’état `stopped` sans route signifie que le pilote a été configuré mais que le callback audio n’a pas démarré. Le serveur HTTP/WebSocket continue de répondre dans cet état. Il reste actif pendant la durée de vie du processus moteur; le client Electron arrête ce processus lorsqu’il se ferme.

## Événements

Chaque événement a la forme `{ "event": "<nom>", "payload": { ... } }` et n’a pas de champ `id`.

| Événement | Payload |
|---|---|
| `engine.status` | État complet `state`, `physicalDriverId`, `sampleRate`, `bufferFrames`, `lastError` |
| `devices.changed` | Inventaires `physicalDrivers` et `virtualDrivers` conformes à `/api/v1/drivers` |
| `routes.changed` | Liste complète et confirmée `routes` |
| `audio.meter` | `endpointId`, `peakDbfs`, compteurs cumulatifs `underruns` et `overruns` |
| `engine.error` | Erreur structurée avec `code`, `message` et, si disponible, opération, pilote et code ASIO |

Les événements de mesure sont émis pour les extrémités présentes dans une route, à la cadence de lecture de l'API (environ deux fois par seconde). `peakDbfs` décrit le bloc audio le plus récent observé à cette extrémité. Les compteurs des endpoints virtuels sont les compteurs cumulatifs du sens correspondant dans l'anneau du client; ceux des endpoints physiques valent zéro. Ces événements ne servent pas de transport audio. La mesure est publiée par le serveur hors du callback temps réel.

## Règles de routage

Une route est canal par canal et contient `id`, `sourceEndpointId`, `destinationEndpointId`, `gainDb` entre -120 et +24 et `mute`. Seuls ces liens sont acceptés :

1. sortie TimoxVasio vers entrée TimoxVasio ;
2. sortie TimoxVasio vers sortie ASIO physique ;
3. entrée ASIO physique vers entrée TimoxVasio.

Les identifiants doivent exister et leurs directions doivent correspondre. Les taux ou formats incompatibles sont refusés ; aucune conversion implicite de fréquence n’est effectuée. TimoxVasio annonce une capacité maximale de 256 entrées et de 256 sorties. L’API publie uniquement les canaux réellement alloués par chaque client, tandis que les endpoints physiques reflètent les capacités découvertes après application du taux.

## Sécurité locale et frontière client

Le moteur ne se lie pas aux interfaces réseau externes. Il accepte les connexions WebSocket locales sans en-tête `Origin` depuis le client natif et l’origine applicative VASIO déclarée ; les autres origines sont rejetées. La GUI obtient les inventaires, routes, erreurs et diagnostics uniquement par cette API. Le format des structures de mémoire audio et leur gestion restent privés au moteur et aux DLL.
