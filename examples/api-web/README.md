# Exemple web : piloter l'API locale

Cette page montre comment une interface indépendante interagit avec TimoxVasio :
lecture de l'état par HTTP, commande du moteur par WebSocket et réception des
événements de niveaux. Elle fonctionne sans connexion Internet et sans serveur web.

## Lancer

1. Démarrer **Timox VASIO Control** (ou `TimoxVirtualAsioEngine.exe`) sur Windows.
2. Ouvrir [`index.html`](index.html) dans un navigateur par double-clic.
3. Vérifier le port local affiché par le moteur. Il utilise normalement `52525`.
4. Cliquer sur **Se connecter**.

La page lit `GET /api/v1/state`, puis ouvre `/api/v1/ws` avec le sous-protocole
`vasio.api.v1`. Les boutons envoient `engine.start` et `engine.stop`. Le panneau
**Échanges avec l'API** affiche les commandes, les réponses et les événements.
Les niveaux apparaissent lorsqu'une application ASIO envoie du son sur une route
active. Une commande refusée affiche le message retourné par le moteur.

Le navigateur utilise ici l'API de contrôle ; le son circule entre les applications
ASIO et le pilote. Pour voir comment une application peut ouvrir le pilote et
traiter ses entrées, voir [l'exemple SuperCollider](../supercollider/README.md).

## Code et contrat

Tout le code nécessaire à l'exécution est dans `index.html` : HTML, CSS et
JavaScript natifs. Aucun CDN ni paquet JavaScript n'est chargé par la page.
Le [contrat OpenAPI](../../api/openapi-v1.json) et le
[quick start API](../../docs/API_QUICKSTART.md) décrivent les autres commandes.

Pour lancer le test automatisé, installer d'abord les dépendances de développement
de `gui/`, puis exécuter depuis la racine du dépôt :

```powershell
node --test examples/api-web/index.test.cjs
```

Le test utilise `jsdom` fourni par les dépendances de développement de l'interface.
