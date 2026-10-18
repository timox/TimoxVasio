# Exemple : utiliser le pilote TimoxVasio depuis SuperCollider

Cet exemple montre comment une application ASIO peut utiliser le pilote : un DAW
(Renoise, Mixxx, etc.) envoie sa sortie sur TimoxVasio, SuperCollider reçoit les
deux canaux ASIO, les traite, et renvoie le son. Le compresseur multibande sert de
traitement illustratif. Pour un exemple centré sur les commandes de l'API locale,
voir l'[interface web hors ligne](../api-web/README.md).

```
DAW ──► TimoxVasio (sortie 1-2) ──► SuperCollider (entrées 1-2)
                                        │  séparation 3 bandes (Linkwitz-Riley 4)
                                        │  compresseur par bande + volumes + limiteur
                                        ▼
                         SuperCollider (sortie 1-2) ──► sortie choisie
```

## Fichiers

| Fichier | Rôle |
|---|---|
| `timox_compresseur.scd` | Démarrage du serveur et interface graphique |
| `compresseur_def.scd` | SynthDef `\timoxCompresseur` commun aux deux modes |
| `render_wav.scd` | Rendu hors ligne d'un WAV stéréo |
| `pilotage/pilote.mjs` | Pilotage en Node.js de l'API TimoxVasio et du compresseur |
| `timox_chaine.scd` | Démo Renoise → SuperCollider → Ableton sur trois paires stéréo |
| `chaine_def.scd` | Saturation, variation du pitch et écho indépendants |
| `horloge_midi.scd` | MIDI Clock autonome corrigé par la fréquence audio mesurée |
| `render_chaine_wav.scd` | Rendu hors ligne de la chaîne à six canaux |

## Démo Renoise → SuperCollider → Ableton

La chaîne prend six entrées ASIO dans cet ordre : grave L/R, médium L/R,
aigu L/R. Elle renvoie six sorties dans le même ordre. Le grave reçoit une
saturation douce, le médium un changement de hauteur, l'aigu un écho. Ableton
reçoit les trois paires sur trois pistes et les mélange vers son Master. Les
numéros de canaux TimoxVasio employés par chaque application sont choisis dans
Control ; les noms des pistes Renoise et Ableton sont libres.

1. Dans Timox VASIO Control, choisir la SSL comme pilote physique, puis régler
   une fréquence et une taille de bloc communes aux trois applications.
2. Relier trois paires Renoise vers les entrées 1–6 de `scsynth.exe`, trois
   paires de ses sorties 1–6 vers Ableton, puis le Master Ableton vers la SSL.
   Vérifier que chaque route est appliquée avant de lancer le son.
3. Fermer tout serveur SuperCollider déjà ouvert. Ouvrir `timox_chaine.scd`
   dans l'IDE et exécuter le bloc entier avec Ctrl+Entrée. Le script démarre
   `TimoxVasio` avec six entrées et six sorties.
4. Choisir dans Renoise les trois sorties stéréo destinées à SC, et dans
   Ableton les trois entrées stéréo venant de SC. Éviter un retour Ableton
   vers une entrée Renoise dans cette démo pour ne pas créer de boucle.

Le nœud audio porte l'identifiant `2000`. Dans l'interpréteur SC :

```supercollider
s.sendMsg('/n_set', 2000, 'pitch', 7);      // médium : +7 demi-tons, de -12 à +12 conseillé
s.sendMsg('/n_set', 2000, 'drive', 3);      // grave : saturation, valeur >= 1
s.sendMsg('/n_set', 2000, 'echoDecay', 2);  // aigu : durée de la traîne en secondes
s.sendMsg('/n_set', 2000, 'midBypass', 1);  // court-circuite le pitch
```

### Horloge MIDI

Le même script charge `horloge_midi.scd`. Il mesure la progression des
échantillons de `scsynth` par rapport au temps système, estime l'écart de
fréquence sur dix secondes et ajuste progressivement le `TempoClock` qui émet
les ticks MIDI. Cette adaptation suit le principe de Mixette, sans dépendre
de Mixxx : le BPM est fixé dans `horloge_midi.scd` et l'écho
utilise une croche à ce tempo. La correction de fréquence vise la dérive
à long terme ; elle ne garantit pas l'absence de gigue à chaque tick.

Avec le [Tool Renoise](../renoise-tool/README.md), les ports Windows MIDI
Services sont créés et sélectionnés par le bouton **Préparer ports MIDI**.
Choisir ensuite l'extrémité d'entrée annoncée par le Tool dans chaque DAW.

Pour utiliser le script SC seul :

1. Dans l'interpréteur SC, exécuter `MIDIClient.init; MIDIClient.destinations.postln;`.
2. Dans l'interpréteur SC, renseigner `~timoxMidiTargets` avec les couples
   `[device, name]` des ports MIDI qui conduisent à Renoise et Ableton, par
   exemple `~timoxMidiTargets = [["Port A", "Port A"], ["Port B", "Port B"]];`.
3. Attendre quelques secondes après le démarrage du serveur, puis exécuter
   `~timoxClockStart.value;`. Configurer Renoise et Ableton en réception de
   MIDI Clock sur ces ports. Pour arrêter : `~timoxClockStop.value;`.
4. Changer le tempo avec `~timoxClockSetBpm.value(128);`. L'écho suit la
   croche correspondante. Le compteur de ticks envoyés est
   `~timoxClockTickCount` ; l'estimation de fréquence est `~timoxClockPpm`.

L'horloge n'émet rien tant qu'aucune destination MIDI n'a été configurée ou
que la référence audio n'a pas été calibrée. Si cette référence cesse de
progresser pendant plus de deux secondes, l'envoi s'arrête.

## Prérequis

- SuperCollider pour Windows avec le support ASIO (rendu testé avec 3.14.1).
- TimoxVasio installé et enregistré pour le mode interactif (voir [INSTALL.md](../../docs/INSTALL.md)).
- Node.js 22 ou plus pour les commandes API WebSocket ; Node.js 18 suffit pour les commandes OSC.

## Lancer le compresseur

1. Démarrer Timox VASIO Control et choisir le pilote ASIO physique.
2. Ouvrir `timox_compresseur.scd` dans SuperCollider.
3. Placer le curseur dans le bloc entre parenthèses et lancer avec Ctrl+Entrée.
4. Le serveur démarre sur le périphérique `TimoxVasio`, deux entrées et deux sorties,
   puis la fenêtre de réglages s'ouvre.

Le nom du périphérique se règle avec la variable d'environnement `TIMOX_DEVICE` si la
carte n'est pas nommée `TimoxVasio`. Les noms disponibles sont listés par
`ServerOptions.devices` dans sclang.

Côté DAW, la sortie stéréo doit être envoyée sur TimoxVasio. Côté routage, Timox VASIO
Control relie la sortie de l'application aux entrées de `scsynth.exe`, puis la sortie de
`scsynth.exe` vers la sortie physique voulue.

## Réglages

- **Entrée / Sortie** : gains en dB, lissés pour éviter les clics.
- **Coupures** : 200 Hz (bas/médium) et 2 kHz (médium/aigu).
- **Seuil et ratio** de chaque bande. Au-dessus du seuil, le gain baisse selon le ratio.
- **Bypass** : court-circuite la compression ; le gain de sortie et le limiteur restent actifs.

## Piloter depuis Node.js

```powershell
# API TimoxVasio : remplacer <port> par le port annoncé par le moteur
$env:TIMOX_API='http://127.0.0.1:<port>'
node pilotage/pilote.mjs etat        # état du moteur, clients, routes
node pilotage/pilote.mjs demarrer    # engine.start
node pilotage/pilote.mjs arreter     # engine.stop

# SuperCollider : scsynth sur 127.0.0.1:57110 par défaut (SC_HOTE, SC_PORT)
node pilotage/pilote.mjs sc-regler loThr -30  # règle un paramètre
node pilotage/pilote.mjs sc-bypass 1          # bypass activé
node pilotage/pilote.mjs sc-arreter           # libère le nœud
node pilotage/pilote.mjs sc-demarrer          # recrée le nœud après sc-arreter
```

Le script SuperCollider crée déjà le nœud 1000 au démarrage. N'utilisez
`sc-demarrer` qu'après `sc-arreter` : une seconde création pendant que le nœud
tourne serait refusée. Les commandes OSC vérifient que `scsynth` répond, mais
son protocole ne confirme pas l'exécution des commandes de réglage. Les curseurs
de la fenêtre ne se synchronisent pas automatiquement avec les changements faits
depuis Node.js ; les déplacer réécrit leur valeur dans le synthé.

## Vérifier le traitement avec un WAV

Le rendu hors ligne utilise le même SynthDef que l'interface et ne nécessite ni
DAW, ni TimoxVasio, ni carte ASIO. Choisissez un WAV stéréo et lancez depuis ce
dossier, dans PowerShell :

```powershell
& 'C:\Program Files\SuperCollider-3.14.1\sclang.exe' .\render_wav.scd 'C:\audio\entree.wav' 'C:\audio\sortie-compressee.wav'
```

Adaptez le chemin de `sclang.exe` à votre installation. La sortie est un WAV
stéréo PCM 16 bits à la fréquence de l'entrée. Le fichier d'origine est lu sans
être modifié.

Pour écouter un rendu PCM 16 bits sur la sortie Windows sans ouvrir de DAW :

```powershell
$player = New-Object System.Media.SoundPlayer('C:\audio\sortie-compressee.wav')
$player.PlaySync()
```

Le [Tool Renoise de démonstration](../renoise-tool/README.md) prépare les routes
Renoise → SuperCollider → Ableton et pilote l'horloge MIDI de cet exemple.

## Limites

- Démonstration, pas un outil de mastering : un seul compresseur par bande, sans
  réglage d'attaque ni de relâchement.
- Le traitement ajoute un bloc de latence, comme tout passage par un pilote ASIO.
- Testé hors ligne sur un signal synthétique et trois boucles WAV stéréo. Le
  démarrage interactif de SuperCollider et l'ouverture de l'interface ont été
  vérifiés avec TimoxVasio ; le routage audio complet depuis un DAW reste à tester.
- Le pilotage WebSocket de l'API n'a pas été testé contre un moteur réel.
