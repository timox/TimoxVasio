# Contrat d’intégration

**Français** | [English](INTEGRATION.en.md)

Le moteur C++ expose le contrat versionné `schemas/api-v1.json` et sa description OpenAPI. Electron démarre et arrête le processus moteur; React n’envoie aucune commande par stdin et ne lit aucun fichier de configuration.

## Démarrage et transport

- Electron démarre `TimoxVirtualAsioEngine.exe` avec stdout/stderr séparés.
- Il extrait le port de l’événement JSON `api.ready`, puis le processus principal consomme `GET /api/v1/state`, `GET /api/v1/drivers` et `ws://127.0.0.1:<port>/api/v1/ws` avec le sous-protocole `vasio.api.v1`.
- Le serveur HTTP/WebSocket utilisé par le moteur est cpp-httplib; le processus principal Electron utilise Fetch et la bibliothèque `ws`.
- Le preload expose uniquement les lectures d’état et d’inventaire, `configuration.apply` et l’abonnement aux événements. React ne connaît pas l’adresse API et n’ouvre pas de socket.

## Configuration

React prépare une configuration complète composée de `physicalDriverId`, des réglages du pilote virtuel unique `virtualDriver` et des `routes`. Chaque modification est soumise par la commande WebSocket `configuration.apply`. Les événements `engine.status`, `devices.changed`, `routes.changed` et `engine.error` reflètent l’état moteur. Un succès déclenche une nouvelle lecture de l’état HTTP.

Les identifiants `sourceEndpointId` et `destinationEndpointId` sont ceux retournés par l’inventaire. L’interface ne calcule pas de canaux, ne crée pas de routes moteur directement et ne conserve pas de configuration persistante parallèle.

Toute modification de configuration arrête le flux audio courant pendant que le moteur remplace le graphe et redémarre le pilote sélectionné.
