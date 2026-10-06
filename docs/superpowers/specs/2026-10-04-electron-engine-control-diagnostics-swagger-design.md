# Timox VASIO Control : configuration, documentation et diagnostic intégrés

## Objectif

Faire de l’exécutable Electron livré avec TimoxVasio le point d’accès direct
pour configurer le moteur, consulter l’API Swagger et diagnostiquer son
fonctionnement. Clarifier l’interface par une navigation courte et des surfaces
plus lisibles, conserver les journaux du moteur et permettre un mode debug sans
écrire de données audio. Livrer l’exécutable portable et l’installateur après
validation fonctionnelle.

## État actuel vérifié

- `gui/dist-candidate/Timox VASIO Control 1.0.0.exe` et `gui/dist-candidate/Timox VASIO Control Setup 1.0.0.exe`
  sont générés. Electron Builder embarque `TimoxVirtualAsioEngine.exe`.
- `gui/electron/main.js` attache un moteur sur le port 52525 ou démarre le
  moteur, puis arrête toujours le processus enfant lors de la fermeture de
  l’application.
- La page React contient déjà les profils par application, le choix du pilote
  physique, la fréquence, la taille de bloc et la matrice de routage. Ces
  fonctions sont réparties dans une longue page sans navigation par fonction.
- Les messages du processus Electron sont écrits dans sa console. Les sorties
  stderr du moteur sont affichées comme notifications éphémères dans l’UI.
  Aucun écran ne conserve ni ne consulte un journal, et aucun mode debug n’est
  pilotable dans l’UI.
- `openapi-v1.json` décrit l’API mais n’est pas intégré à l’interface Electron.
  L’API accepte configuration et profils, mais n’expose pas les diagnostics ni
  une commande d’arrêt ordonné.
- L’audit UX statique relève un texte opérationnel souvent à 12–13 px, un cyan
  décoratif trop présent, des surfaces d’encarts peu différenciées et des
  règles de focus incohérentes. Une capture de Timox VASIO Control reste à
  examiner avant la validation visuelle finale.

## Décisions d’architecture

### Interface de contrôle

L’application garde un seul exécutable Electron. Son en-tête affiche l’état du
moteur et un contrôle d’accès aux fonctions. La zone principale est organisée
en trois vues :

1. **Configuration** : sections existantes de profils, horloge physique,
   clients, canaux et routage, alimentées exclusivement par les routes et
   événements de l’API documentée.
2. **API** : Swagger UI chargé depuis les ressources empaquetées et alimenté
   par le fichier OpenAPI local. L’interface n’utilise aucun CDN ni service
   réseau externe. La partie WebSocket, qui ne se rend pas comme une route REST
   OpenAPI standard, affiche aussi les commandes et exemples définis dans
   `x-websocket`.
3. **Diagnostic** : état de connexion, moteur et clients, niveau de journal,
   activation du mode debug, dernières lignes de journal, bouton de
   rafraîchissement et export du journal.

Le preload Electron n’expose que des fonctions étroites et validées. Le renderer
ne lit ni fichier local ni mémoire partagée. Les profils, configurations, états,
diagnostics et actions du moteur passent par des routes ou commandes documentées
de l’API. Les fonctions purement OS, telles qu’ouvrir le fichier exporté ou
démarrer un exécutable, passent par des IPC dédiés avec validation stricte.

### Cycle de vie du moteur

Electron conserve le comportement de connexion/démarrage automatique. La vue
Journaux ajoute les actions démarrer et arrêter, avec état et résultat
confirmés. L’arrêt exige zéro client
ASIO connecté. Un moteur déjà présent mais non démarré par cette fenêtre n’est
jamais tué directement : il reçoit la commande API d’arrêt ordonné, sous la
même règle zéro client. Au lancement, Electron vérifie d’abord l’API sur le
loopback, et ne crée pas de second moteur lorsqu’un moteur TimoxVasio valide
répond déjà.

La fermeture de la fenêtre ne termine pas un moteur qui dessert encore un
client. Le processus moteur est détaché de la fenêtre et continue son cycle de
vie; l’arrêt reste une action explicite lorsqu’aucun client n’est connecté.
L’état de propriétaire ou d’attachement et les résultats des actions sont
présentés à l’utilisateur.

### API diagnostic et journal

Le moteur écrit des entrées horodatées dans
`%LOCALAPPDATA%\TimoxVasio\logs\engine.log`, avec quatre archives de 5 Mio
maximum chacune. Chaque entrée porte un
niveau, un composant et un message. Les événements incluent démarrage/arrêt,
API, ouverture ASIO, paramètres physiques confirmés, clients, configuration,
erreurs et compteurs de pertes disponibles hors callback.

Les callbacks audio n’écrivent jamais de journal et ne font aucun accès disque,
allocation ou attente supplémentaire. Aucun échantillon audio ni contenu de
buffer n’est enregistré. Le niveau normal est `info`; le mode debug ajoute des
traces de contrôle et d’allocation. Le niveau est persisté dans
`%LOCALAPPDATA%\TimoxVasio\diagnostics.json`. `GET
/api/v1/diagnostics?limit=N` retourne le niveau et au plus 500 entrées les plus
récentes; la valeur par défaut est 200. `PUT /api/v1/diagnostics` remplace le
niveau avec `{ "level": "info" }` ou `{ "level": "debug" }`. La commande
WebSocket `{ "id": "stop-1", "command": "engine.stop" }` demande un arrêt ordonné et est refusée avec
`ENGINE_CLIENTS_CONNECTED` si un client ASIO est encore attaché. La réponse
d’acceptation est envoyée avant le signal d’arrêt. Les réponses et erreurs sont
décrites dans les schémas, OpenAPI, Swagger et tests de contrat.

### Présentation visuelle

- Une pile de police unique et une taille de base de 16 px.
- Les textes d’aide et résumés courants utilisent au moins 14 px; les cellules
  denses de la matrice utilisent au moins 13 px.
- Le cyan sert aux actions et repères principaux; les états utilisent des
  couleurs sémantiques accompagnées d’un libellé.
- Les cartes utilisent deux surfaces clairement distinctes, des bordures
  neutres lisibles et une échelle commune de rayons.
- Les champs, boutons et tableaux partagent une hauteur et un focus visible
  cohérents. Les halos décoratifs et styles hérités inutilisés sont supprimés
  lorsque le rendu confirme qu’ils ne sont plus utiles.
- La hiérarchie Configuration/API/Diagnostic doit rester utilisable à 1400 ×
  900 et sur une fenêtre réduite.

### Livraison

Le build produit l’exécutable portable et l’installateur Windows depuis la
même version de l’interface, du moteur et d’OpenAPI. La publication cible le
dépôt public TimoxVasio. Le tag et les assets ne sont créés qu’après validation du build,
des tests et de la découverte Mixxx déjà confirmée.

## Contrats attendus

- `GET /api/v1/diagnostics?limit=N` retourne `{level, entries}` avec des lignes
  `{timestamp, level, component, message}`. `N` est entier entre 1 et 500; sa
  valeur par défaut est 200.
- `PUT /api/v1/diagnostics` accepte exactement `{level}` où `level` vaut
  `info` ou `debug`; il retourne le niveau confirmé et refuse tout champ
  inconnu ou niveau non reconnu.
- `engine.stop` est une commande WebSocket avec payload `{}`. Elle refuse toute
  demande pendant qu’au moins un client est attaché, puis publie
  `engine.status: stopped` et ferme proprement l’API et le contrôleur.
- Étendre OpenAPI et `schemas/api-v1.json`; documenter chaque réponse, erreur,
  limite de lecture et transition d’état.
- Étendre le client API et preload uniquement avec les opérations nécessaires
  aux trois vues et aux commandes de cycle de vie.
- La configuration audio et ses routes restent au format complet
  `configuration.apply`; aucune route de compatibilité historique n’est
  conservée.

## Vérification et critères d’acceptation

1. L’interface empaquetée ouvre Configuration, Swagger et Diagnostic sans
   serveur externe; Swagger affiche le document de la version livrée et la
   commande WebSocket `configuration.apply`.
2. Les réglages physiques et les routes continuent de provenir de l’API. Une
   configuration complète peut être appliquée et relue dans l’état confirmé.
3. Démarrer ne lance jamais deux moteurs. Arrêter/redémarrer est refusé si un
   client est connecté. Fermer l’UI laisse le moteur actif lorsqu’un client
   l’utilise.
4. Les journaux persistent après fermeture de l’UI, tournent à la limite
   définie, peuvent être consultés/exportés et contiennent assez d’information
   pour distinguer démarrage, échec API, ouverture ASIO et reconfiguration.
5. Le mode debug peut être activé/désactivé via l’API, son état est confirmé
   dans l’UI et ne modifie pas le traitement audio temps réel.
6. Les tests de contrat couvrent succès, limites, erreurs et arrêt protégé par
   client connecté. Les tests UI couvrent navigation, réglages et diagnostic.
7. Une capture du build Electron montre la lisibilité des textes, la hiérarchie
   des surfaces et le focus clavier. Les builds portable et installateur
   embarquent les mêmes sources API et moteur.
8. Après publication, le tag et les assets existent sur le dépôt public
   TimoxVasio. Le chemin audio SSL 12 reste une validation distincte : au moment
   de cette conception, l’API indique encore zéro route et le moteur est arrêté.

## Hors périmètre

- Enregistrer, analyser ou transmettre des échantillons audio dans les
  diagnostics.
- Exposer l’API de contrôle au réseau; elle reste liée à `127.0.0.1`.
- Publier sur un dépôt sans lien avec TimoxVasio.
- Modifier les préférences Mixxx ou le projet source Mixxx.
- Déclarer le signal audio validé sans une lecture effective et une observation
  mesurée des sorties physiques.
