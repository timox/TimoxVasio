# Timox VASIO Control

**Français** | [English](README.en.md)

Interface Electron et React de configuration du moteur VASIO. L’interface consomme le contrat déclaré dans `../schemas/api-v1.json` : HTTP pour l’état et l’inventaire, WebSocket `vasio.api.v1` pour `configuration.apply` et les événements du moteur.

## Démarrage en développement

Depuis `gui/` :

```powershell
npm install
npm start
```

Le processus Electron s’attache à un moteur déjà actif sur le port local ou démarre `TimoxVirtualAsioEngine.exe`, puis consomme ses routes HTTP et sa WebSocket. La navigation propose Configuration, Canaux, Analyse, API et Journaux. La matrice de routage se déplie dans Canaux lorsqu’elle doit être modifiée. Swagger UI et les schémas sont produits depuis `../openapi-v1.json` et `../schemas/api-v1.json`, puis embarqués avec leurs ressources locales par `scripts/embed-openapi.js`.

En développement et lors de l’empaquetage, Electron utilise `../build_codex_110/Release/TimoxVirtualAsioEngine.exe` et son dossier `config/`. `npm run dist` vérifie avant le packaging la présence des binaires et les SHA-256 du moteur, du pilote et du ZIP pilote. Après packaging, il compare le moteur embarqué avec le build source et écrit un manifeste dans `dist/release-validation-1.1.0/`. La DLL ASIO est distribuée séparément; elle n’est pas incluse dans l’application Electron.

## Utilisation

1. Régler les profils de canaux par application. Les profils existants viennent de `GET /api/v1/application-profiles`; « Enregistrer les profils » remplace la liste via `PUT /api/v1/application-profiles`.
2. Fermer puis relancer les applications signalées par l’API pour qu’elles annoncent les nouveaux comptes. Le profil initial de `mixxx.exe` est 255/255; les applications sans profil gardent 256/256.
3. Choisir un pilote ASIO physique et appliquer. Le moteur ouvre alors le pilote choisi et publie ses endpoints dans l’inventaire.
4. Sélectionner des endpoints de sortie et d’entrée pour construire les routes. Les points disponibles viennent exclusivement de l’API.
5. Régler le gain et le mute, puis appliquer la configuration complète.

Si le panneau des profils indique une route HTTP 404, le moteur auquel l’application est connectée est antérieur à cette API. Reconstruire et relancer `TimoxVirtualAsioEngine.exe`, puis reconnecter l’interface.

Chaque application remplace la configuration moteur et interrompt le flux audio le temps du changement. Le statut et les erreurs affichés viennent des événements de l’API.

## Vérifications

```powershell
npm run contract-test
npm run react-build
```

Les commandes vérifient le façonnage du contrat sans dépendance tierce et compilent l’interface React avec l’API intégrée hors ligne.

## Diagnostic et arrêt

La vue Journaux lit le moteur via `GET /api/v1/diagnostics` et change le niveau via `PUT /api/v1/diagnostics`. Elle permet aussi d’arrêter puis redémarrer le processus. L’arrêt documenté est protégé : le moteur répond `ENGINE_CLIENTS_CONNECTED` tant qu’une application ASIO est attachée. Fermer la fenêtre Electron ne termine pas un moteur encore utilisé. Les journaux persistants sont enregistrés sous `%LOCALAPPDATA%\TimoxVasio\logs\engine.log`; quatre archives de 5 Mio sont conservées au maximum.
