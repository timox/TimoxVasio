# TimoxVasio unique, 256 canaux par direction et horloge ASIO physique

## Contexte et décision

Le modèle actuel expose quatre DLL COM, VASIO1 à VASIO4, chacune avec six
entrées et six sorties. Cette séparation a été choisie comme commodité de
routage, mais limite chaque application et multiplie les identités de pilotes.
Le nouveau modèle expose une seule DLL ASIO `TimoxVasio.dll`, enregistrée sous
le nom `TimoxVasio`, avec une capacité maximale de 256 entrées et 256 sorties. ASIO distingue ces deux nombres dans
`getChannels`.

La capacité de base est de 256 entrées et sorties. Un profil API identifié par
le nom de l'exécutable peut réduire séparément les comptes annoncés à une
application donnée; les applications sans profil gardent 256/256. Le profil
initial de `mixxx.exe` est 255/255 pour contourner la représentation de compte
de canal de la version stable. Les profils sont lus avant `getChannels` et
s'appliquent à `getChannels`, `getChannelInfo` et `createBuffers`. Ils ne
modifient pas les 256 slots du transport partagé. Chaque application choisit
ensuite ses canaux réels via `createBuffers`; le moteur suit séparément les
canaux actifs de chaque processus. Le PID et le nom de processus identifient
les clients dans l'inventaire API.

Le pilote ASIO physique choisi est l'horloge maître. Sa fréquence et la taille
de buffer sélectionnées parmi ses capacités sont les valeurs effectives du
graphe et du pilote TimoxVasio. TimoxVasio annonce seulement ces valeurs; toute demande
d'une application pour une autre fréquence ou taille est refusée. Les anneaux
interprocessus transportent les blocs entre callbacks; ils ne sont pas une
conversion de fréquence ou une politique de tailles de blocs indépendantes.

Le moteur natif est distribué sous le nom `TimoxVirtualAsioEngine.exe`. Il reste
distinct de la DLL ASIO `TimoxVasio.dll` et pilote le périphérique physique ainsi
que les mappings clients.

## Coût mémoire

Le transport garde deux anneaux par client, chacun contenant 16 384 frames de
float32 pour 256 canaux. Leur contenu représente exactement :

```text
2 directions × 16 384 frames × 256 canaux × 4 octets = 33 554 432 octets = 32 Mio
```

Les pages de ce mapping sont partagées entre l'application cliente et le
moteur, elles ne sont pas dupliquées pour ces deux processus. Chaque processus
client a son propre mapping : deux applications à pleine capacité représentent
64 Mio pour les anneaux; quatre en représentent 128 Mio. Les buffers ASIO
double-buffer et les buffers de travail du graphe s'ajoutent à ces valeurs et
dépendent de la taille de bloc et des canaux actifs.

## Inventaire et transport client

- Le format partagé porte 256 slots d'entrée et 256 slots de sortie, ainsi que
  les masques ou listes des canaux réellement fournis à `createBuffers`.
- Le numéro API reste indexé à partir de 1; `ASIOBufferInfo::channelNum` reste
  indexé à partir de 0.
- Le moteur expose les endpoints d'un client seulement pour ses canaux actifs.
  Un hôte qui n'alloue que quelques canaux ne crée donc pas des centaines de
  ports utilisables sans buffers.
- `createBuffers` accepte jusqu'à 512 entrées de structure au total, sous
  réserve que chaque index soit inférieur à 256 et ne soit pas dupliqué dans
  la même direction.
- Le protocole mémoire partagée change de version. Un ancien pilote six canaux
  et le moteur nouveau ne peuvent pas attacher le même mapping.
- Le moteur traite uniquement les canaux actifs du client lorsqu'il copie les
  blocs et construit le graphe.

## Host physique et graphe

L'inventaire API d'un périphérique physique reflète les comptes retournés par
son pilote ASIO. Il expose les ports physiques disponibles, tandis que
`PhysicalAsioHost` ne crée des buffers que pour l'union des canaux physiques
référencés par les routes appliquées. Les index de canal du matériel restent
ceux retournés par son pilote; aucun remappage implicite n'est effectué.

Lorsqu'une configuration contient au moins une route mais que cette union est
vide (par exemple une route virtuelle vers virtuelle), certains hôtes ASIO
refusent `createBuffers` avec zéro descripteur. Le moteur crée alors un unique
buffer interne de cadence, sur la première entrée physique disponible (ou, à
défaut, la première sortie qu'il maintient silencieuse). Ce buffer n'est ni une
extrémité API ni une route et ne change pas l'ensemble des canaux physiques
routés. Pour la configuration initiale sans aucune route, le pilote physique
reste ouvert et ses capacités sont publiées, mais le flux ASIO n'est pas démarré
avant qu'une route soit appliquée.

Les trois liens traversent le même `RoutingGraph` typé :

1. sortie TimoxVasio d'un processus vers entrée TimoxVasio d'un autre;
2. sortie TimoxVasio vers sortie physique;
3. entrée physique vers entrée TimoxVasio.

Le callback physique est l'horloge et l'appel de traitement principal. Chaque
client TimoxVasio annonce et crée des buffers à la même fréquence et à la même taille
que le moteur physique sélectionné. Tout changement de périphérique, fréquence,
taille de buffer, client ou route arrête le flux, reconstruit les buffers et le
graphe, puis redémarre ou laisse le moteur en erreur arrêté.

Après avoir sélectionné la fréquence physique, le moteur relit les capacités
qui peuvent varier avec cette fréquence, notamment `getChannels` et
`getBufferSize`, avant de publier l'inventaire et de créer les buffers. La
capacité physique n'est pas codée en dur. Sur SSL 12, SSL documente 12 entrées
et 8 sorties, dont l'entrée ADAT peut fournir 8 canaux à 44,1/48 kHz, 4 à
88,2/96 kHz et 2 à 176,4/192 kHz ([caractéristiques](https://www.solidstatelogic.com/products/ssl-12),
[guide](https://support.solidstatelogic.com/hc/en-gb/articles/5568765809309-SSL-12-User-Guide)).

## API et interface

- L'inventaire contient un seul pilote virtuel `TimoxVasio`.
- `GET` et `PUT /api/v1/application-profiles` lisent et remplacent la liste
  complète des plafonds par application. Les comptes valides vont de 1 à 256;
  une application absente conserve 256/256. Le `PUT` retourne les clients
  actifs qui doivent être redémarrés pour prendre un changement de profil.
- L'état d'un client comprend son PID, son nom d'application et les canaux
  d'entrée/sortie effectivement alloués.
- Les identifiants virtuels ont la forme
  `virtual:TimoxVasio:<pid>:input:<channel>` et
  `virtual:TimoxVasio:<pid>:output:<channel>`.
- La configuration globale sélectionne le pilote physique, une fréquence et
  une taille de buffer qu'il annonce, ainsi que les routes complètes. Elle ne
  contient aucune fréquence ou taille de buffer indépendante pour TimoxVasio.
- L'API d'état expose les valeurs physiques confirmées après application; les
  getters ASIO TimoxVasio ne publient que ces mêmes valeurs. Les demandes de fréquence ou
  de buffer incompatibles sont rejetées avant de modifier le mapping client.
- L'interface affiche les ports découverts par l'API et permet de relier les
  sources virtuelles ou physiques aux destinations autorisées.
- `TimoxVasio` reste énumérable par les hôtes ASIO quand le moteur est arrêté;
  l'initialisation de métadonnées ne crée aucune horloge audio. Une allocation
  de buffers exige une connexion au moteur et à son horloge physique, sinon le
  pilote renvoie une erreur de connexion explicite.
- Une commande `configuration.apply` interrompt le routage pendant tout
  changement effectif. L'API publie les transitions d'état et l'erreur
  structurée en cas d'échec.
- L'installation enregistre un seul pilote. La migration retire les
  enregistrements VASIO obsolètes pour éviter que quatre noms continuent à
  apparaître comme s'il s'agissait de quatre moteurs indépendants.

## Vérification requise

- Une sonde COM constate 256 entrées et 256 sorties sur l'unique pilote.
- Une sonde exécutée sous `mixxx.exe` constate 255/255, tandis qu'un hôte sans
  profil constate 256/256; `createBuffers` refuse tout index supérieur au
  compte annoncé dans chacune des deux directions.
- Une sonde crée des buffers sur des indices bas, élevés et clairsemés, dans
  les deux directions, et valide les échantillons transportés.
- Deux processus ASIO simultanés disposent chacun de leurs canaux et endpoints
  actifs sans collision d'identifiants.
- Les sondes du graphe vérifient les trois types de liens avec des canaux
  physiques dont les index dépassent 6.
- Le probe physique vérifie que le moteur ne crée que les buffers routés et
  relit comptes, fréquences et tailles après application de la fréquence.
- Les probes TimoxVasio vérifient que `getSampleRate` et `getBufferSize` reflètent
  les valeurs physiques confirmées et qu'une autre demande est rejetée.
- L'API et l'interface montrent les clients, leurs endpoints actifs, les
  capacités physiques et les interruptions de routage.
- Un hôte ASIO réel, en particulier Mixxx, valide la découverte et l'allocation
  de canaux au-delà de 6. L'acceptation matérielle observe un signal sur des
  canaux SSL 12 et ADAT réels; compiler ou énumérer un pilote ne suffit pas.

## Limites et mesures

La limite annoncée de 256 par direction est le plafond du pilote TimoxVasio,
pas le nombre de canaux du périphérique physique. L'API présente uniquement
les canaux physiques réellement retournés après sélection du taux. Les compteurs
de pertes et des mesures de signal/latence devront établir le comportement sur
machine, sans déduire une latence d'une simple somme de tailles de buffers.
