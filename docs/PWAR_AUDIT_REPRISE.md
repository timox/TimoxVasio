# Note de reprise — audit PWAR

Date : 24 octobre 2026. Note préparée pour reprendre le travail PWAR séparément
de la stabilisation Windows. Elle rassemble les preuves locales et les étapes
à qualifier ; les pistes de performance ci-dessous sont des hypothèses, pas des
résultats de benchmark.

## Usage visé

PWAR doit permettre de faire circuler le son entre un hôte ASIO Windows et le
graphe PipeWire Linux. Selon le besoin, le flux Windows peut être traité par un
outil Linux, renvoyé vers Windows, ou utilisé dans un seul sens. Le protocole
audio PWAR reste le plan de données. Le contrôle de Timox par API depuis Linux
est un chantier distinct et n'est pas implémenté.

PWAR relie des canaux explicitement ouverts et routés vers son endpoint ASIO ;
il ne détecte ni ne redirige les flux arbitraires des processus Windows. Seuls
les canaux effectivement raccordés à l'endpoint entrent dans le graphe Timox.

## État vérifié dans ce dépôt

### Mise à jour du 25 octobre 2026

- Le relevé plus détaillé de [`PWAR_PROTOCOL_REQUALIFICATION.md`](PWAR_PROTOCOL_REQUALIFICATION.md)
  poursuit l'audit après les patches 0008 et 0009 : tests Windows 39/39, tests
  routeur consolidés 34/34 et 16 cas du banc Linux isolé sous ASan/UBSan. Ces
  résultats restent des tests de code ; ils ne qualifient pas le relais audio
  sous PipeWire ni le réseau réel. Le bundle Windows à sept patches ne contient
  toujours pas les candidats plus récents.
- Les patches 0009 et 0010 sont maintenant présents dans `patches/pwar/`. Le
  0010 valide la configuration CLI avant le démarrage, mais annonce aussi la
  limite actuelle de 128 frames. Cela prévient une configuration trompeuse ;
  ce n'est pas la prise en charge des quanta PipeWire plus grands.
- Le source Linux candidat confirme le blocage : dans `on_process`, si le
  quantum réel dépasse 128 frames, les sorties sont mises à zéro et un xrun est
  compté. À 512 ou 1024 frames, le pont ne transporte donc pas le son. Le
  `passthrough_test` local contourne le transport et ne prouve pas le cas réseau.
- Le format réseau borne chaque datagramme à 128 frames et porte
  `seq`, `num_packets` et `packet_index`. Or `process_ping_pong` émet aujourd'hui
  un seul paquet par callback PipeWire (`num_packets = 1`) : il ne découpe pas
  les quanta pour le pair Windows.
- Le listener ASIO Windows remplace actuellement `num_packets` par
  `blockFrames / chunk_size` et rejette les fragments si le bloc ASIO n'est pas
  divisible par la taille du premier fragment. Cela ne suffit pas pour un
  fragment final court (p. ex. 192 frames en 128+64). Une fois l'assemblage
  complet, le code copie `blockFrames` depuis `input_buffers` même si le routeur
  annonce moins de `samples_ready` ; une trame manquante ou partielle ne peut
  donc pas être traitée comme un bloc ASIO complet sans reblocage explicite.
- Les quanta PipeWire et ASIO ne sont pas négociés par le protocole actuel. Le
  transport doit donc accumuler/découper des frames bornées à chaque extrémité
  et préserver l'ordre des séquences ; corriger uniquement le nombre de
  fragments ne résout pas le décalage entre deux tailles de callback.
- Les chemins `process_one_shot` et `process_ping_pong` exécutent encore le
  `sendto` et prennent un mutex depuis le callback PipeWire ; le mode oneshot
  attend aussi le retour réseau. Le callback doit être découplé du transport
  avant de prétendre à une exécution temps réel stable.
- Le travail Windows/Linux reste séparé du contrôle API distant de Timox. La
  présence de l'archive `build_pwar_linux_qualification.tgz` ne vaut pas preuve
  de compilation ou d'essai PipeWire : consulter le journal daté et le hash
  transféré avant de s'en servir.

- Base amont examinée : `ripxorip/PWAR`, commit
  `ed5b07c44c7635d006422d646d56368600504b3f`.
- Les correctifs `0001` à `0006` couvrent surtout le pilote Windows : bornes du
  routeur, arrêt du listener avant libération, validation des datagrammes,
  canaux stéréo/partiels, résultat de démarrage et journalisation réseau
  désactivée par défaut. Les sources et résultats détaillés sont dans
  [`patches/pwar/README.md`](../patches/pwar/README.md).
- DLL Windows candidate à six correctifs :
  `build_pwar_qualification/Release/PWARASIO.dll`, 166912 octets,
  SHA-256 `EAB1067A785D3AA25A241D0DD3122263558D555459879060866AA62B0D982EB2`.
  Le banc de la DLL passe **18/18** et le banc avec l'hôte `PhysicalAsioHost`
  passe **21/21**. Ces résultats couvrent chargement local par fabrique,
  callbacks et canaux ; ils ne qualifient ni l'enregistrement COM réel, ni le
  graphe Timox complet, ni un pair Linux ou les performances réseau.
- Correctif Linux candidat `0007-linux-guard-callback-input-and-quantum.patch` :
  il protège `on_process` contre une entrée DSP nulle et empêche de passer un
  quantum supérieur aux 128 frames du paquet fixe. En cas de quantum trop grand,
  il produit du silence et compte un xrun. Le banc de callback est dans
  [`linux_callback_tests.cpp`](../patches/pwar/linux_callback_tests.cpp), mais
  cette note ne confirme pas encore son exécution, sa compilation Linux, ni un
  essai PipeWire réel.
- La GUI Windows règle l'adresse et le port de destination du pilote ; le port
  d'écoute Windows reste `8321`. Elle n'installe pas et ne démarre pas le relais
  Linux. Voir [`PWAR_GUI_TESTS.md`](PWAR_GUI_TESTS.md).

## Constats et limites

### Établis dans le source amont épinglé

- `linux/libpwar.c:on_process` peut recevoir un pointeur d'entrée DSP nul et le
  transmettre aux chemins de copie.
- Le paquet audio réserve 128 frames par chunk alors que le callback reçoit son
  quantum de PipeWire. Un quantum supérieur à cette capacité atteint des copies
  qui peuvent dépasser les tableaux du paquet.
- GUI et CLI Linux utilisent la même bibliothèque : changer d'interface n'évite
  pas ces défauts.
- Le pilote Windows d'origine a des limites de cycle de vie et de sélection de
  canaux. Les correctifs Windows les traitent dans le banc local ; ils ne prouvent
  pas la réception audio depuis PipeWire.

### Pas encore démontrés

- La cause exacte du SIGSEGV utilisateur dans `memcpy`/AVX-512 : il manque la
  backtrace et la provenance/hash du binaire. Le nom de la routine ne prouve pas
  un problème d'alignement.
- La stabilité sous charge, la dérive d'horloge, les pertes UDP, la latence
  bout-en-bout et le coût CPU sur Linux/Windows.
- La capacité à traiter puis retourner le signal dans une chaîne PipeWire réelle.

Voir [`PWAR_LINUX_STARTUP_CRASH.md`](PWAR_LINUX_STARTUP_CRASH.md) et
[`pwar-integration-assessment.md`](pwar-integration-assessment.md) pour les
observations source détaillées.

## Suite recommandée

1. Obtenir la backtrace du crash et identifier le binaire réellement utilisé
   (version, chemin, architecture, SHA-256). Ne pas attribuer le crash à
   l'alignement sans preuve.
2. Appliquer `0007` à une extraction propre du commit amont épinglé, compiler la
   cible Linux et lancer le banc du vrai callback. Compléter par un essai de
   démarrage avec PipeWire, puisque le CLI et la GUI partagent la bibliothèque.
3. Remplacer le silence permanent pour les quanta supérieurs à 128 par un
   traitement par chunks bornés, sans allocation dans le callback. Vérifier
   continuité des frames, ordre stéréo, séquences/horodatages, xruns et quanta
   64, 96, 128, 256 et 512 ; garder une politique explicite si le pair ne peut
   pas suivre.
4. Faire un essai audio bidirectionnel sur câble réseau : Windows → Linux,
   traitement PipeWire, puis Linux → Windows. Répéter en sens unique pour
   distinguer les deux chemins. Consigner taux, buffers, mode oneshot,
   buffers variables, pertes, xruns, charge CPU et latence p50/p95/max.
5. Après une base stable et mesurée, profiler avant de toucher aux copies,
   allocations, tailles de buffers ou à la cadence réseau. Garder le format et
   le protocole PWAR existants ; toute optimisation doit conserver des mesures
   comparables avec TimoxVasio.

## Reprise rapide

- État de l'intégration Windows : [`pwar-integration-assessment.md`](pwar-integration-assessment.md).
- Procédure d'essai utilisateur : [`PWAR_GUI_TESTS.md`](PWAR_GUI_TESTS.md).
- Historique et tests des correctifs : [`patches/pwar/README.md`](../patches/pwar/README.md).
- Correctif Linux candidat : [`0007-linux-guard-callback-input-and-quantum.patch`](../patches/pwar/0007-linux-guard-callback-input-and-quantum.patch).

Ne pas inclure la DLL PWAR candidate dans une distribution Timox et ne pas la
présenter comme qualifiée avec Linux avant les essais réseau et PipeWire réels.
