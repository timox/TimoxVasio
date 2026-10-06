# Conception : pilotes virtuels ASIO et routage applicatif

**Français** | [English](2026-10-02-virtual-asio-routing-design.en.md)

> **Conception antérieure — état historique.** Les principes de routage restent du contexte, mais l’état vérifié ci-dessous et les noms de pilotes/exécutable sont périmés. Pour le contrat courant et ses critères d’acceptation, consulter [la spécification TimoxVasio](2026-10-03-vasio-single-driver-256-channel-design.md) et [le plan maître](../plans/2026-10-03-vasio-256-physical-master-plan.md).

## Objectif

Fournir sous Windows x64 un pilote ASIO virtuel utilisable par des applications
audio, annonçant jusqu’à 256 entrées et 256 sorties, et une application
graphique permettant de construire, modifier et diagnostiquer des circuits
entre les canaux actifs de ces applications, les entrées d’un pilote ASIO
matériel et ses sorties physiques.

Le pilote ASIO matériel sélectionné dicte les modalités du transport audio :
le moteur adopte la fréquence d’échantillonnage et la taille de bloc effectives
du périphérique physique, puis présente ces mêmes valeurs aux applications
connectées au pilote virtuel. Une demande incompatible est refusée. Le graphe
peut relier des applications virtuelles entre elles ou les relier aux entrées
et sorties physiques; le nombre de canaux virtuels actifs est déterminé par
les buffers effectivement demandés par chaque application, dans la limite de
256 par direction.

Le premier périmètre source comprend les applications qui sélectionnent
explicitement VASIO comme pilote ASIO. La capture des sorties Windows partagées
(WASAPI) n’en fait pas partie.

## État vérifié au 2 octobre 2026

- La cible CMake par défaut compile `VirtualASIO.exe` sous VS 2026 et embarque
  le serveur HTTP/WebSocket local basé sur cpp-httplib. `configuration.apply`
  reste désactivé tant que le contrôleur audio n’est pas raccordé.
- `VASIO_BUILD_DRIVERS=ON` compile quatre DLL COM. L’énumérateur Steinberg
  découvre les pilotes après installation et les instancie.
- Les DLL utilisent le mapping partagé versionné. `DriverAudioProbe` attache
  un moteur de test, remplit les buffers d’entrée et vérifie les buffers de
  sortie sur six canaux pour les quatre DLL. Sans moteur, `init()` refuse le
  démarrage avec une erreur explicite.
- `PhysicalAsioHost::enumerate()` retourne les CLSID et noms des entrées ASIO
  externes sans ouvrir de périphérique ; la sonde locale en observe 29, y
  compris des pilotes virtuels tiers. L’ouverture du CLSID choisi, la lecture
  de ses capacités, la création/destruction de buffers stables et le callback
  matériel sont implémentés et compilent. Les conversions PCM courantes sont
  traitées dans ce callback ; l’activation réelle d’un périphérique sélectionné
  et son raccordement au graphe restent à vérifier.
- `VasioClientManager` détecte les processus chargeant l’une des quatre DLL,
  valide le mapping versionné, s’y attache et détache les clients disparus.
  L’intégration au moteur par défaut est compilée ; un probe enfant vérifie
  l’attachement interprocessus.
- `openapi-v1.json` et `schemas/api-v1.json` décrivent le contrat HTTP et
  WebSocket ; le serveur natif loopback sert l’inventaire, l’état provisoire et
  le handshake WebSocket. `configuration.apply` est transmis au contrôleur,
  qui arrête le flux, valide les endpoints, remplace le runtime et reprend
  l’horloge si la configuration réussit. Un probe local couvre les branches
  `stopped` et `error` sans ouvrir de matériel.
- `RoutingGraph` compile et traite les trois directions de route supportées,
  avec gain et mute ; ses tests sont verts. `AudioRoutingRuntime` relie les
  buffers ASIO float32 aux anneaux VASIO, et son probe confirme des échantillons
  pour les routes physique→VASIO, VASIO→physique et VASIO→VASIO.
- `AudioController` possède `PhysicalAsioHost` sur un thread dédié avec fenêtre
  cachée et boucle de messages Windows. Ce cycle de vie compile; l’ouverture et
  le callback d’un appareil matériel réel ne sont pas encore vérifiés.
- La GUI Electron/React appelle encore les anciennes commandes stdin
  `routes.add/remove/list`; elle ne consomme pas encore ce contrat.

## Architecture retenue

### Plan de contrôle

Un processus moteur natif, lancé dans la session Windows de l’utilisateur,
héberge l’API locale documentée et reste distinct de la fenêtre Electron. La
fenêtre Electron démarre ou rejoint ce moteur et utilise uniquement l’API
HTTP/WebSocket pour lire l’état et demander des changements. Aucun module UI ne
lit le registre, `routing.ini`, la mémoire partagée audio ou un détail interne
du moteur.

L’API est la seule frontière de configuration et de contrôle. Elle documente
les schémas des périphériques, capacités, ports virtuels, routes, erreurs et
états. Les commandes mutantes sont idempotentes. Les événements WebSocket
publient les changements d’inventaire, d’état de moteur et de diagnostic, dont
les pertes de buffer et la dernière erreur. Le stockage interne reste privé au
moteur et ne constitue pas un contrat client.

Toute mutation de configuration interrompt explicitement le routage avant
d’appliquer le changement. Si le moteur était actif et que la nouvelle
configuration est valide, le moteur recrée buffers et graphe puis reprend le
routage. Si l’application échoue, il reste arrêté et publie l’erreur ; il ne
continue jamais silencieusement avec un état ancien ou partiellement appliqué.
L’API expose les transitions `running`, `reconfiguring`, `stopped` et `error`
afin que la GUI rende l’interruption visible.

### Plan audio

Une DLL VASIO est un pilote Windows COM qui implémente `IASIO` selon le modèle
de l’exemple Steinberg fourni. Elle possède un CLSID stable, une entrée ASIO et
un chemin `InprocServer32` correct. Elle annonce une capacité maximale de 256
entrées et 256 sorties. Chaque application sélectionne ses canaux utilisés via
`createBuffers`; le moteur n’expose comme endpoints que les canaux actifs de ce
client. L’identité du driver est unique et les identités des canaux sont
stables.

Le transport partagé versionné réserve 256 slots par direction pour chaque
client. La version du protocole change avec cette disposition, et un driver ou
moteur de version incompatible refuse l’attachement. Les noms de driver ne
servent plus à répartir les ports de routage.

Le moteur ouvre un seul pilote ASIO matériel sélectionné à la fois et en fait
l’horloge maître. La fréquence et la taille de bloc effectives du pilote
physique déterminent celles du graphe et sont les seules annoncées par VASIO.
Les clients doivent accepter exactement ces valeurs; une demande différente
est refusée avant de modifier leur mapping. Après `setSampleRate`, le moteur
relit les capacités physiques qui peuvent dépendre de la fréquence, puis crée
les buffers des seuls canaux matériels référencés par les routes appliquées.
Son callback matériel traite le graphe audio et échange les blocs avec les
files des clients VASIO. Les files servent à l’échange interprocessus et ne
convertissent ni la fréquence ni la taille de bloc. Le callback matériel et
les callbacks ASIO des clients ne font ni allocation, ni accès disque, ni
attente sur un verrou bloquant. Les notifications et statistiques sont
publiées hors du callback.

Le graphe supporte ces liens explicites :

1. sortie VASIO d’une application vers entrée VASIO d’une autre application ;
2. sortie VASIO vers sortie du périphérique ASIO matériel ;
3. entrée du périphérique ASIO matériel vers entrée VASIO d’une application.

Les routes sont canal par canal, ont un identifiant stable et peuvent exposer
gain et mute. Un format ou une fréquence non pris en charge est refusé avec une
erreur d’API explicite avant activation. Aucune conversion implicite de
fréquence ou de taille de bloc n’est autorisée. Les underflows et overflows des
files interprocessus sont comptés et signalés. Un underflow fournit des zéros
pour les frames manquantes. Si un bloc ne tient pas dans une file, le
producteur le rejette sans bloquer et incrémente le compteur d’overflow ; il
ne modifie pas l’index de lecture possédé par le consommateur.

### Installation et cycle de vie

L’installeur copie la DLL x64, enregistre idempotemment son unique entrée ASIO
et sa clé COM, puis vérifie les chemins et CLSID enregistrés. La migration
retire les seules anciennes entrées VASIO1 à VASIO4 et leurs CLSID connus; les
autres pilotes ASIO ne sont pas modifiés. La désinstallation retire uniquement
les clés correspondant aux identifiants VASIO documentés. La configuration
des pilotes n’effectue pas d’écriture directe depuis la GUI.

Le moteur est lancé dans la session utilisateur, car il pilote le périphérique
ASIO matériel. Si le moteur est absent, le pilote virtuel retourne une erreur
de connexion explicite et ne simule pas de flux audio. Fermer la fenêtre ne
coupe pas un moteur qui dessert encore des clients. L’application de contrôle
peut masquer sa fenêtre ; le moteur reste disponible tant qu’un client VASIO
est connecté et peut être arrêté explicitement quand aucun client n’utilise le
circuit.

## Découpage de livraison

Chaque étape doit rester vérifiable avant d’aborder la suivante :

1. **Pilote ASIO conforme et enregistrable** : implémentation COM fondée sur
   l’exemple du SDK, un CLSID stable, exports et registre conformes, build de
   l’unique DLL, découverte et instanciation par l’énumérateur ASIO du SDK,
   puis annonce et allocation de canaux jusqu’à 256 par direction.
2. **Transport interprocessus et moteur maître** : protocole mémoire partagée
   versionné à 256 canaux par direction, files sans verrou bloquant, gestion de
   connexion et de cycle de vie, moteur qui applique la fréquence et la taille
   de bloc du pilote matériel et publie ses capacités. Les clients sont exposés
   au contrôleur via des snapshots qui épinglent leur mapping.
3. **Graphe audio et API** : routes entre les trois types de ports, validations,
   application atomique des mutations, persistance moteur, API HTTP/WebSocket,
   événements et schémas, diagnostics.
4. **Interface graphique** : inventaire et capacités issus de l’API, éditeur
   de circuit, paramètres de pilote et de route, états et diagnostics. Pas de
   données métier codées en dur ni de stockage de routes parallèle.
5. **Validation de bout en bout** : installation et découverte dans un hôte
   ASIO, lecture d’une application vers sortie physique, entrée physique vers
   application, liaison entre deux applications, changement de taux/buffer,
   déconnexion/reconnexion et observation des pertes/latences.

## Critères d’acceptation

- L’unique DLL x64 est enregistrée sous son CLSID stable, découverte et
  instanciée dans un hôte ASIO réel; elle annonce 256 entrées et sorties et
  alloue des canaux bas, élevés et clairsemés.
- L’installation remplace les anciennes entrées VASIO1 à VASIO4 sans toucher
  aux autres pilotes ASIO.
- La fréquence et la taille de bloc VASIO sont égales aux valeurs physiques
  effectives; les demandes incompatibles sont refusées.
- L’inventaire API reflète les vrais ports VASIO et le pilote physique choisi.
- Un circuit réglé dans l’interface passe par l’API, change effectivement le
  flux audio et survit à un redémarrage du moteur.
- Chaque changement de configuration arrête le flux avant mutation, puis
  reprend après succès ou reste arrêté avec une erreur explicite.
- Les trois chemins audio listés ci-dessus sont vérifiés avec des signaux
  mesurables et des canaux physiques identifiables.
- Une incompatibilité de fréquence ou de format est refusée visiblement ; une
  panne ou perte de bloc est remontée par API et GUI.
- Les callbacks audio restent sans allocation, accès disque ni verrou bloquant.

## Décisions hors périmètre

- Capturer les applications qui utilisent la sortie Windows partagée.
- Intercepter une application qui ouvre directement un autre pilote ASIO.
- Ouvrir plusieurs pilotes ASIO matériels simultanément dans la première
  version ; un seul périphérique physique est l’horloge maître.
- Ajouter des effets DSP au graphe.
- Modifier un circuit sans interrompre le routage audio.
- Traiter `routing.ini`, la structure interne des DLL ou les anciens fichiers
  d’architecture comme contrats d’API.
