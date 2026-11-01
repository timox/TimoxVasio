# Firefox vers SSL : son haché signalé le 27 octobre

**Retour utilisateur du 28 octobre :** le problème a été identifié en présence
d'Ableton ouvert, même inactif. Le mécanisme précis (pilote, horloge, routage ou
accès concurrent) n'a pas été mesuré. Les observations historiques suivantes
ne démontrent donc pas une faute de SSL 360 ni une sommation comme cause.

L'utilisateur décrit une forte dégradation, puis un son « tout haché, inaudible ».
Les observations ci-dessous sont des lectures de l'API et des processus, sans
redémarrage, modification de routage ou changement de buffers.

## Exécution réellement observée

- Moteur PID 20088 : `build_gui_api_20261026/TimoxVSSC/artifacts/backend/TimoxVirtualAsioEngine.exe`.
- SHA-256 : `a8f52830e161699607cdcac44b5499c28456f71c2417a1977d91ef63c18210b5`.
- Ce moteur est antérieur au binaire `da90a3c…` livré avec la GUI supervision2.
  Ouvrir une GUI plus récente n'a donc pas remplacé le moteur déjà lancé.
- Horloge ASIO SSL ASIO Driver 1, 48 000 Hz, blocs de 2048 trames (42,67 ms).
- Pont `FIREFOX_AUDIO-COPY`, Firefox PID 17444, génération du pont 2.
  Capture PCM16 stéréo 44 100 Hz, paquets/buffer natif de 441 trames ;
  rééchantillonnage vers le graphe 48 000 Hz.
- Quatre routes actives : gauche/droite du pont vers SSL 1/2 et 3/4, gains 0 dB.
  Les nombreuses autres routes sauvegardées ne sont pas des routes actives.
- Firefox est aussi associé à la sortie Windows « Output 5/6 (SSL 12 USB Audio Device) ».
  Un mélange de l'écoute directe et de la copie retardée est une hypothèse à
  vérifier, pas une cause établie.

## Mesures capturées

Six relevés entre 17:41:16 et 17:41:19, heure locale :

- Trames capturées : 5 635 980 à 5 762 988.
- Trames rejetées/perdues à l'écriture capture : 2292, compteur constant sur la fenêtre.
- Silences injectés par la lecture du FIFO : 0 ; discontinuités : 0.
- Remplissage FIFO : 6876 à 9074 trames, capacité 16384.
- Correction du ratio : environ −575 à −474 ppm, nominal 48000/44100.
- Aucun dépassement de durée de traitement ni démarrage retardé comptabilisé
  dans les fenêtres moteur observées ; pire traitement autour de 0,34 ms.

Un relevé ultérieur conserve 2292 rejets, zéro silence et zéro discontinuité,
avec 20 024 928 trames capturées. Ces compteurs n'expliquent pas le hachage.
L'état moteur `healthy` ne qualifie pas le signal sonore.

Une capture WebSocket ultérieure de six secondes donne des pics identiques
entre la source du pont et les sorties SSL correspondantes, autour de −87 à
−85 dBFS. La présence d'une lecture musicale pendant cette fenêtre reste à
confirmer. Des pics identiques ne démontrent pas l'identité des formes d'onde.

## Preuves locales et limites

Les snapshots et événements horodatés sont conservés dans le dossier ignoré
`build_processcopy_20261027/` : `live-state.json`,
`live-degradation-samples.json`, `live-state-after.json`,
`live-state-chopped.json`, `live-applications.json`, `live-meter-events.json`.
Aucun enregistrement du contenu sonore n'a été réalisé.

L'ancien pont `ssssssssss`, Firefox PID 16152, est `unavailable` ; ses ports
de brouillon/sauvegardés expliquent sa présence indue dans l'ajout de connexion.
Le pont `OUTTOJABRAH` est en échec/réessai, sans route active dans ce trajet.

La cause du hachage n'est pas établie. Il faut distinguer le signal reçu du
processus, le traitement du pont et l'écoute physique avant de modifier le DSP.

## Compléments utilisateur et supervision

L'utilisateur confirme que la lecture continue pendant les niveaux proches de
−85 dBFS. Une seconde fenêtre présente des pics entre environ −87 et −13 dBFS,
toujours identiques entre la source du pont et les sorties correspondantes.
Il indique avoir coupé une des écoutes sans amélioration ; le point exact de
cette coupure n'est pas encore identifié. Le simple mélange direct/copie ne
suffit donc pas à expliquer le problème observé.

Demande complémentaire : détecter les sommations. La spec de supervision
intègre la distinction entre sommation interne observée et doublon externe
suspect. Dans ce relevé, chaque sortie SSL active ne reçoit qu'une source
Timox ; la copie vers plusieurs sorties distinctes n'est pas en elle-même
une sommation. Le mixage éventuel dans la SSL n'est pas connu de cette API.

## Fin de la fenêtre d'observation

Une sonde COM en lecture seule relève ensuite une session Firefox PID 21060
inactive sur SSL Output 3/4, volume de session 1, non muette, pics nuls sur
50 mesures. Les endpoints Windows Output 5/6 et Monitors L/R sont également
à pic nul ; ce dernier est réglé à volume 0 et muet. Ces observations portent
sur des sessions/endpoints Windows, pas sur le signal ASIO ou le mixeur matériel.
Fichier : `build_processcopy_20261027/windows_session_observation_all.json`.

Le dernier relevé API (`live-state-final.json`) n'a plus de session audio
Windows active, le pont Firefox est `unavailable` et aucune route n'est active.
Il ne doit pas remplacer les preuves précédentes prises pendant la dégradation.

Tests isolés PCM, SRC et contrôleur WASAPI réussis. Aucun défaut DSP reproduit.
Les sources PCM/SRC/backend/FIFO/worker sont identiques entre le moteur en cours
et le nouveau lot ; mettre à jour ce dernier n'est donc pas présenté comme
un correctif du hachage.
