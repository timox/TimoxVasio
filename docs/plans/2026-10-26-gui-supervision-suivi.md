# GUI, mesures et projection API — 26 octobre 2026

## Décisions confirmées

- L'adhérent (processus ou périphérique) possède plusieurs nœuds possibles.
- Le moteur gère les liens entre ports de nœuds. Les adhérents peuvent demander
  une connexion, mais n'en assurent pas la gestion.
- Le moteur produit les observations, référencées aux nœuds/ports ou liens.
  Le lissage des vumètres et les couleurs restent de la présentation GUI.
- Un niveau source combiné au gain ne constitue pas une mesure du lien.
- Capacités inconnues, notamment traitement DAW et MIDI non observé : `null`.
  Aucune politique d'éjection automatique n'est implémentée dans ce lot.

## Répartition et périmètre

Le parent corrige la GUI et contrôle l'intégration ; le sous-agent
`api_supervision` construit la projection moteur, les événements, les schémas,
les tests et le classement des propriétés GUI. `review_supervision_spec`
relit la GUI ; `review_imports` relit la projection API.

Les travaux sont préparés dans `build_gui_api_20261026/TimoxVSSC` et
`build_gui_api_20261026/TimoxVAudioEngine`, à partir des dépôts extraits.
Les sources sont préparées ici avant intégration dans les dépôts extraits.
Les changements préexistants dans le dépôt d'origine sont conservés.

## Premier lot : vumètres, projection et Swagger

`devices.changed` incluait les compteurs d'activité et le CPU processus ; ces
variations provoquaient une remise à zéro des vumètres entre deux mesures.
La correction distingue topologie et observations. Les snapshots périodiques
invalident aussi une ancienne mesure en cas de remplacement du processus ou
d'arrêt moteur. Une activité explicitement indisponible masque le niveau.

Les 18 tests React ciblés passent après reproduction des défauts, ainsi que
les 34 tests Node/Electron/contrat initiaux. Un test Swagger supplémentaire
vérifie les 11 modèles nommés et leurs descriptions ; les deux tests Swagger
passent. La projection inclut les accès natifs, distingue les niveaux reçus
d'un processus des niveaux produits par le moteur vers ce processus et sépare
lecture API et acquisition audio. Build moteur, tests purs de projection,
tests HTTP/WebSocket et validation JSON Schema des réponses : réussis.

À la demande de l'utilisateur, la GUI de travail a été lancée avec le moteur
récent (API 1.1, supervision 1) ; le moteur signale `running`. Ce lancement ne
constitue pas une qualification audio matérielle. Aucun test réseau PWAR réel.

Portable produit : `build_gui_api_20261026/TimoxVSSC/dist/TimoxVSSC-0.1.0-preview.exe`,
71 563 652 octets, SHA-256
`75c7ee198dedc5aece4459f61d89d36a3cc9be7ef21a38230808706b7f5c7ae4`.
Extraction de l'EXE et comparaison moteur/PWAR/schémas/Swagger embarqués :
réussies. Le portable lui-même n'a pas été lancé. Les changements suivants
ne sont pas automatiquement présents dans cet artefact.

Limite persistante : les niveaux sont les dernières valeurs sans timestamp
d'acquisition ; `readAtUnixMs` ne garantit pas leur fraîcheur.

## Deuxième lot : autres écrans et livrables

- `api_other_parts` : conserver les observations et raisons d'échec d'un pont
  WASAPI dont le rôle par défaut ne résout aucun endpoint, sans inventer d'adhérent.
- `gui_other_parts` : supprimer l'identification PWAR par nom de processus,
  conserver les inconnus et distinguer demande WASAPI et état observé.
- `deliverables_inventory` : identifier livrables, consommateurs, chemins et
  niveau de validation de chaque dépôt.
- Parent : revue, contrôles communs, provenance et intégration ciblée des sources.

Le moteur et la GUI déjà ouverts ne sont pas interrompus pour ce travail.

## Résultat du deuxième lot — 27 octobre

Corrections validées : observations WASAPI conservées pour rôle non résolu,
identifiants inconnus explicitement nuls, repli vers endpoint configuré si
information effective partielle ; GUI PWAR identifiée par PID courant du
superviseur sans comparaison fictive d'identité, inconnus conservés et anciennes
observations purgées à la reconnexion. WASAPI sépare demandé et observé.

Le parent a relancé 35 tests React ciblés, 35 tests Node/Electron/contrat,
SupervisionApiTests et ApplicationProfilesApiTests ; tous passent. Validation
JSON Schema des réponses réelles HTTP/WS et du cas de pont non résolu : réussie.
Compilation moteur et GUI, packaging portable et contrôle après extraction :
réussis. Pas de nouvelle qualification audio réelle.

Sources et artefacts intégrés dans `../TimoxVAudioEngine` et `../TimoxVSSC`,
branches `api/supervision-20261026`, commités et poussés le 27 octobre : moteur
`fb7113d`, GUI `6d882df`. 114 fichiers vérifiés,
sauvegardes sous `build_gui_api_20261026/previous-targets/`, journal
`build_gui_api_20261026/transfer-result.json`. Pas de modification pilote/PWAR.
Les inventaires complets et les nouveaux chemins sont dans
[les livrables par dépôt](2026-10-26-livrables-par-depot.md).

Le dernier candidat GUI reproductible est sous
`build_gui_api_20261026/release/TimoxVSSC`, distinct de la copie GUI ouverte.
Ne pas rejouer `finalize-manifest.cjs` sans préserver la liste des changements
ou la recalculer depuis la base canonique : le script a été utilisé une fois.
