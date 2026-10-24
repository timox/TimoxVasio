# Aide contextuelle et vues par sujet

## Retour utilisateur

La séparation de l’aide est utile mais son contenu reste trop général. Chaque
réglage ambigu doit expliquer la décision qu’il demande. Les valeurs observées
ne doivent pas ressembler à des réglages. Le routage est apprécié : conserver
son graphe et ses interactions, mieux identifier ASIO/WASAPI et fournir une
légende dans un onglet d’aide. Analysis, Performance et API doivent être divisés
par sujet avec une aide séparée.

## Vues proposées

| Espace | Sous-onglets | Contenu de l’aide |
|---|---|---|
| WASAPI | Devices / Live status / Help | Mode, endpoint fixe ou rôle par défaut, disponibilité, format, files audio |
| Routing | Graph / Help | Types de blocs, sens des ports, états des câbles, compte des canaux, actions |
| Analysis | Timeline / Transport / Stereo correlation / Help | Période d’observation, compteurs et incréments, interprétation de la corrélation |
| Performance | Callback / Applications / Transport / Stage measurements / Help | Délai par bloc, CPU processus et callback, pertes transport, limites des mesures |
| API | Swagger / WebSocket / Console / Help | Référence HTTP, enveloppe commande/événement, exécution réelle et erreurs |

Les onglets changent la vue, pas les abonnements audio ou les brouillons. Le
graphe Routing reste monté quand Help est visible pour conserver son état local.
Les contrôles Start/Stop analysis et Measure stages conservent leurs fonctions
explicites ; ouvrir un onglet ne lance pas une mesure ou une commande API.

## Aide au point de décision

**Mode** répond à « dans quel sens circule le son ? » : Capture reçoit un
microphone ou une entrée Windows et fournit des sorties au graphe ; Render reçoit
des entrées du graphe et joue vers une sortie Windows ; Endpoint loopback reçoit
le son joué sur une sortie Windows et le fournit au graphe. Expliquer pourquoi
le mode ne peut pas changer tant que des routes utilisent les ports.

**Endpoint selection** répond à « ce matériel précis ou le choix de Windows ? » :
Specific endpoint conserve l’ID choisi ; Windows default suit le périphérique
par défaut du rôle choisi. Console, Multimedia et Communications sont des rôles
Windows, pas des modes audio ni des noms de périphériques. Afficher dans le suivi
l’endpoint effectivement résolu. Le champ doit avoir un bouton d’aide proche
dont le nom accessible nomme explicitement le réglage.

**Frames currently queued** est une mesure, pas une capacité à saisir. La placer
dans Live status sous une liste de valeurs avec unités « graph frames ». Distinguer
capacité de file, remplissage actuel, silences et rejets. Le nombre de frames seul
n’est pas une latence de bout en bout ; sa conversion temporelle requiert la
fréquence du graphe et ne décrit que cette file.

Chaque aide contextuelle doit contenir : sens du champ, exemple concret, effet
du choix, limite importante. Les erreurs et conséquences immédiates restent
près du contrôle. L’aide générale peut compléter sans répéter tout le formulaire.

## Distinction des blocs Routing

Remplacer les petits symboles Unicode seuls par badges compacts associant SVG et
texte : ASIO, WASAPI, App, Profile, Saved. Ils utilisent la ligne de titre existante,
sans modifier largeur de bloc, coordonnées ou ports. Le texte reste lisible sans
couleur et indique notamment qu’App représente une application via le pilote
virtuel, tandis qu’ASIO représente ici l’interface physique.

L’aide réemploie les mêmes badges et dessine les motifs de câbles avec CSS :
trait plein, tirets pending, points muted, surbrillance sélection. Les couleurs
de câble choisies par l’utilisateur sont conservées et ne servent pas de code
ASIO/WASAPI. Une connexion active ne prouve pas qu’un signal est audible.

## Validation

Vérifier les contrôles clavier des onglets, les relations ARIA, les brouillons
et filtres conservés, l’absence d’appels API en ouvrant une aide, les métriques
inaccessibles dans Devices et accessibles dans Live status. Pour Routing,
vérifier identité DOM du graphe à l’aller-retour Help, badges sur vrais types de
blocs, couleurs personnalisées, menus contextuels et absence de panneau
permanent. Les illustrations de légende sont produites avec les composants
réels ; elles évitent des captures périmées ou dépendantes du matériel local.
