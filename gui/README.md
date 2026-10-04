# VASIO Control

Interface Electron et React de configuration du moteur VASIO. L’interface consomme le contrat déclaré dans `../schemas/api-v1.json` : HTTP pour l’état et l’inventaire, WebSocket `vasio.api.v1` pour `configuration.apply` et les événements du moteur.

## Démarrage en développement

Depuis `gui/` :

```powershell
npm install
npm start
```

Le processus Electron démarre `TimoxVirtualAsioEngine.exe`, lit l’événement de démarrage `api.ready`, puis son processus principal consomme les routes HTTP et la WebSocket de l’API. Le preload expose à React uniquement les lectures d’état et d’inventaire, l’application d’une configuration complète et l’abonnement aux événements.

Le binaire attendu en développement est `../build_engine_vs2026_ninja/TimoxVirtualAsioEngine.exe`. Electron Builder embarque le backend depuis ce même emplacement.

## Utilisation

1. Choisir un pilote ASIO physique et appliquer. Le moteur ouvre alors le pilote choisi et publie ses endpoints dans l’inventaire.
2. Sélectionner des endpoints de sortie et d’entrée pour construire les routes. Les points disponibles viennent exclusivement de l’API.
3. Régler les options VASIO, le gain et le mute, puis appliquer la configuration complète.

Chaque application remplace la configuration moteur et interrompt le flux audio le temps du changement. Le statut et les erreurs affichés viennent des événements de l’API.

## Vérifications

```powershell
npm run contract-test
npm run react-build
```

Le premier vérifie le façonnage du contrat sans dépendance tierce. Le second compile l’interface React.
