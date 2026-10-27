# Mixette consommatrice de Timox — diagnostic horloge du 27 octobre

Demande : vérifier ce qui manque pour utiliser Timox depuis Mixette SCEngine,
après un essai rapporté non fonctionnel en horloge logique/logicielle.
Audit du clone local `../grrzzzz/mixette-SCEngine` et du moteur extrait
`../TimoxVAudioEngine`. Aucune modification de leurs sources ni action audio.

## Constats reproduits côté consommateur

- `api/classes/Config.js:202` limite la configuration à sept champs sans
  `clockSource` et exige un pilote physique lors de la sauvegarde.
- `api/classes/TimoxVasio.js:39` et `:46` interdisent conflits/application Live
  lorsque `physicalDriverId` est nul, même pour une configuration sauvegardée.
- `api/services/timoxvasio-routing.js:47` exige un pilote physique ; `:66`
  exige ses capacités. Le payload construit à `:159` omet `clockSource`.
  La confirmation à `:270` ne vérifie pas non plus la source d'horloge.
- Master/Cue sont construits vers des endpoints physiques SSL. Sous horloge
  logicielle, ces routes physiques restent sauvegardées mais dormantes côté
  moteur ; cadence de calcul et périphérique de restitution sont distincts.

Trois reproductions sans réseau ni audio, exécutant les modules réels Mixette,
confirment rejet d'une sélection software, rejet du passage Live correspondant,
et omission de `clockSource` dans le payload matériel. Script :
`build_clock_audit_20261027/mixette-clock-check.mjs` (3/3 assertions réussies,
qui confirment les limitations actuelles, pas un correctif).

## Ce que fournit déjà Timox

`configuration.apply` accepte `clockSource:{kind:"software"}` avec
`physicalDriverId:null`, fréquence entière 8000–384000 Hz et bloc entier
16–8192 trames. Exemple sans routes :

```json
{"id":"software","command":"configuration.apply","payload":{"clockSource":{"kind":"software"},"physicalDriverId":null,"sampleRate":48000,"bufferFrames":256,"routes":[]}}
```

`GET /api/v1/state`, `engine.status` et `supervision.engine` exposent source,
format, capacités d'horloge, état et erreur. L'omission de `clockSource`
préserve la sélection existante ; elle ne choisit pas automatiquement software
avec un pilote nul, ni ASIO avec un pilote fourni. Voir
`src/control_api_server.cpp:839`, `:965` et `src/audio_controller.cpp:539`.
Un payload matériel Mixette peut donc être incompatible avec une sélection
software préexistante, au lieu de rétablir explicitement ASIO.

Le moteur sait démarrer software sans carte ni client et publier le format
aux clients ASIO. Les tests existants couvrent ce chemin, notamment
`tests/application_profiles_api_tests.cpp:258` et
`tests/software_clock_routing_tests.cpp:40`. Leur présence ne qualifie pas
la chaîne Mixette/audio réelle.

## Manques de diagnostic côté Timox

- L'API expose activité, échanges et âge du dernier callback, mais pas la
  génération audio attendue/acquittée par chaque client ni la cause détaillée
  d'un refus de démarrage du pilote. Un client sans échanges reste ambigu.
- `src/vasio_driver.cpp:316` du pilote affiche « physical ASIO clock » quand
  le transport n'est pas prêt : message trompeur en mode software, condition
  réelle indépendante de la présence d'une carte physique.
- Le log détaillé après configuration (`control_api_server.cpp:975`) est
  réservé au cas physique, alors que l'état HTTP décrit aussi software.
- `bufferHealth` fournit déjà une observation de cadence sur fenêtres de
  cinq secondes (callbacks, retards, dépassements, marge). Le champ nominal
  `sampleRate` seul ne prouve pas le respect des échéances.

## Limites et suite

La capture exacte de l'essai rapporté n'a pas été retrouvée. Les mentions
historiques « horloge logicielle » dans la reprise Mixette concernent notamment
MIDI ; elles ne démontrent pas une panne de cadence audio software Timox.
Ne pas attribuer la cause du dernier essai aux seuls blocages statiques trouvés.

Prochain travail à cadrer : consommation explicite du contrat d'horloge par
Mixette, destination audio valide dans le mode choisi, et diagnostic API de
l'accord de format/génération entre moteur et adhérents. Pas d'optimisation
topologique automatique ni d'éjection introduite par cet audit.
