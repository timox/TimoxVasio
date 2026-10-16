# Tool Renoise : routage TimoxVasio

Cet exemple reprend l'idée du contrôleur Renoise → SuperCollider de
`grrzzzz/renoise-tools/com.renoise-supercollider.controller.xrnx` : une fenêtre
Renoise pilote un composant externe. L'ancien exemple prévoyait VB-Cable pour
l'audio et des messages OSC pour les effets. Ici, le Tool utilise l'API locale
TimoxVasio pour les routes audio et OSC pour piloter l'horloge MIDI dans
SuperCollider. Le dossier `grrzzzz` n'est pas modifié.

## Chaîne de démonstration

```text
3 pistes Renoise (sorties stéréo distinctes)
  → 3 entrées stéréo SuperCollider (1–6)
  → 3 sorties stéréo SuperCollider (1–6)
  → 3 entrées stéréo Ableton
  → Master Ableton → sortie physique SSL
```

Les trois pistes Renoise peuvent recevoir les bandes grave, médium et aigu
avec `#Multiband Send`. L'exemple SC correspondant est
[`../supercollider/timox_chaine.scd`](../supercollider/timox_chaine.scd).
Le Tool lit le routage de sortie **actuel** des trois pistes choisies. Les noms
des pistes n'ont aucune importance. Le plan comporte 14 routes mono et conserve
les autres routes enregistrées. Il privilégie trois paires d'entrée Ableton
sans route enregistrée ; une route déjà présente reste autorisée et signalée.

## Préparer

1. Démarrer Timox VASIO Control, choisir la SSL comme interface physique et
   démarrer le moteur. Renoise, SuperCollider et Ableton doivent ouvrir
   TimoxVasio en ASIO pour que leurs canaux apparaissent dans l'API.
2. Dans Renoise, affecter trois pistes de traitement à trois sorties stéréo
   TimoxVasio distinctes. Les sorties 1–2, 3–4, 5–6 conviennent pour commencer.
   Couper toute autre sortie de ces pistes qui créerait un chemin audio parallèle.
3. Le bouton **Démarrer SC** du Tool lance
   [`../supercollider/timox_chaine.scd`](../supercollider/timox_chaine.scd)
   avec `sclang` et charge automatiquement l'horloge MIDI. Le serveur SC doit
   disposer des entrées et sorties 1–6 de TimoxVasio. **Arrêter SC** demande
   l'arrêt seulement au processus lancé par ce compagnon.
4. Dans Ableton, activer au moins trois paires d'entrées TimoxVasio. Créer
   trois pistes audio dont les entrées correspondent aux paires annoncées par
   l'aperçu du Tool ; router ces pistes vers Master. Le Master doit sortir sur
   une paire TimoxVasio ouverte, par défaut 1–2.

## Lancer le Tool

Node.js 22 ou plus récent est requis pour le compagnon local. Depuis la racine
du dépôt :

```powershell
node examples/renoise-tool/bridge.mjs
```

Il écoute seulement sur `127.0.0.1:52999` et contacte l'API TimoxVasio sur
`http://127.0.0.1:52525`. Si le moteur utilise un autre port :

```powershell
$env:TIMOX_API = 'http://127.0.0.1:52526'
node examples/renoise-tool/bridge.mjs
```

Installer le fichier `com.timoxvasio.routing-demo.xrnx` dans Renoise, ou copier
le dossier source `com.timoxvasio.routing.xrnx` dans le dossier `Tools` de Renoise.
La commande apparaît dans **Tools → TimoxVasio Routing Demo**. Le compagnon
Node doit rester ouvert pendant l'usage du Tool.

1. Cliquer **État** et vérifier les trois applications actives.
   Si SC n'apparaît pas, cliquer **Démarrer SC**, puis **État**. Le chemin par
   défaut est `C:\Program Files\SuperCollider-3.14.1\sclang.exe` ; définir
   `SCLANG_PATH` avant de lancer le compagnon si SC est ailleurs.
2. Sélectionner successivement les pistes grave, médium et aigu dans Renoise et
   cliquer **Prendre la piste sélectionnée** dans la ligne correspondante.
3. Adapter les noms des exécutables si l'état affiche d'autres noms, puis
   cliquer **Aperçu des routes**. Contrôler les paires Ableton affichées. Si
   l'aperçu signale une route directe déjà présente vers la paire SSL, cocher
   **Remplacer les routes existantes vers cette paire SSL** puis refaire
   l'aperçu pour que Live soit seul à alimenter cette paire.
   Les listes **Master paire** et **SSL paire** désignent toujours une paire
   stéréo complète, par exemple `1-2` ; choisir `1-2` pour utiliser les sorties
   physiques SSL 1 et 2.
4. Configurer les entrées des trois pistes Ableton sur ces paires. Cliquer
   **Appliquer**. Le Tool relit l'état API et affiche combien de routes sont
   enregistrées et actives. Vérifier aussi l'état appliqué dans Control.

`configuration.apply` enregistre la configuration complète du moteur. Le Tool
préserve les autres routes et remplace ses propres routes lors d'une nouvelle
application. L'option SSL retire uniquement les anciennes routes dont la
destination est l'un des deux canaux physiques choisis ; elle est désactivée
par défaut. L'aperçu expire si l'état du moteur change entre aperçu et
application. Cette démonstration ne change ni les sorties internes de Renoise
ni les entrées des pistes Ableton : les choisir dans leurs interfaces.

## Transport OSC facultatif

Le Tool préremplit **Renoise TCP** avec `127.0.0.1:8000` et **AbletonOSC UDP**
avec `127.0.0.1:11000`. Dans Renoise, activer le serveur OSC en mode TCP dans
les [préférences OSC](https://tutorials.renoise.com/wiki/Open_Sound_Control).
Dans Live, activer la [surface de contrôle AbletonOSC](https://github.com/ideoforms/AbletonOSC).
Les boutons
**Start OSC** et **Stop OSC** envoient des commandes de transport aux deux DAW.
Le protocole TCP Renoise utilise ici des paquets OSC encadrés en SLIP. L'envoi
UDP vers AbletonOSC ne confirme pas que Live a exécuté la commande.

Ces commandes démarrent ou arrêtent la lecture ; elles n'émettent aucun tick
MIDI Clock. Pour une synchronisation musicale continue, configurer l'horloge
MIDI ci-dessous.

## Horloge MIDI

Après **Démarrer SC**, cliquer **Préparer ports MIDI**. Le compagnon utilise la
console Windows MIDI Services pour créer, si besoin, deux paires de bouclage
MIDI, selon le même principe que Mixette : une extrémité de sortie pour SC et
une extrémité d'entrée pour chaque DAW. Il repère les sorties dans
SuperCollider et configure les destinations sans recopier de noms internes.
La console `midi` doit être installée sur Windows. Les ports créés par cette
commande sont temporaires et peuvent devoir être recréés après un redémarrage
du service MIDI ou de Windows ; le même bouton le fait sans créer de doublon.

Dans Renoise, choisir `TimoxVasio Renoise Clock` comme **MIDI Clock Slave** et
activer le mode de synchronisation MIDI Clock. Dans Live, activer **Sync In**
sur `TimoxVasio Ableton Clock`, puis **Ext**. Régler le BPM et cliquer **Start**
dans le Tool. L'horloge SC attend quelques secondes de mesures audio avant de
démarrer.
Le bouton **Stop** arrête l'émission. Les commandes OSC sont envoyées sans
accusé de réception : vérifier le journal de SuperCollider si la commande ne
produit aucun effet.

L'API TimoxVasio pilote ici les **routes audio**. Les commandes MIDI Clock
passent par OSC vers `sclang` sur le port 57120 (`SC_LANG_PORT` pour changer
ce port dans le compagnon).

## Vérification développeur

```powershell
node --test examples/renoise-tool/bridge.test.mjs
```

Les tests vérifient le plan des routes et sa conservation des routes existantes.
`tool_smoke.lua` simule aussi un clic sur le choix de piste avec un objet Song
qui refuse la comparaison Lua `==` ; il s'exécute avec l'interpréteur Lua livré
avec Renoise.
La fenêtre XRNX et le son de bout en bout nécessitent une session Renoise,
SuperCollider, Ableton et une interface SSL disponibles.
