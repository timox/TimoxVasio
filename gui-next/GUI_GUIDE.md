> Imported historical feature guide. For current standalone build and artifact paths, use README.md and MIGRATION.md.

# Guide d’utilisation de TimoxVSSC

**Français** | [English](GUI_GUIDE.en.md)

## Configuration audio

L’interface présente les pilotes ASIO physiques découverts par le moteur et l’unique pilote virtuel `TimoxVasio`. Sélectionner le pilote physique, puis choisir une fréquence et une taille de buffer parmi les capacités qu’il annonce. La même fréquence et la même taille sont utilisées par le moteur et les clients TimoxVasio.

L’inventaire API présente les applications connectées avec leur PID, leur nom et les canaux réellement alloués par chacune. Les endpoints virtuels disponibles correspondent à ces canaux actifs. Les ports physiques sont ceux retournés par le pilote après application de la fréquence.

## Profils de canaux par application

Le panneau « Profils de canaux par application » lit et remplace la liste complète par les routes HTTP documentées `GET` et `PUT /api/v1/application-profiles`. Un profil associe un nom d’exécutable à des nombres distincts d’entrées et de sorties, chacun compris entre 1 et 256. La liste vide revient au défaut 256/256; le profil initial `mixxx.exe` est 255/255.

L’API retourne les clients dont la capacité annoncée va changer. Fermer puis relancer chaque application indiquée : une instance ASIO déjà initialisée conserve ses comptes jusqu’à son prochain démarrage. Les profils ne réduisent pas la capacité du transport partagé ni les routes entre applications.

Si le panneau signale que la route n’est pas disponible (HTTP 404), le moteur connecté doit être recompilé et redémarré avec une version qui inclut l’API des profils.

## Sélection dans une application audio

Dans un hôte ASIO, choisir l’API son ASIO puis le périphérique `TimoxVasio`. Après l’installation du pilote, fermer puis relancer l’hôte pour renouveler sa liste. Mixxx 2.6 beta x64 retire actuellement ce pilote de sa liste parce qu’il annonce 256 entrées et sorties; voir [l’analyse de compatibilité Mixxx](../docs/mixxx-256-channel-compatibility.md). `TimoxVasio` reste visible pour la découverte même si le moteur n’est pas prêt; pour ouvrir un flux audio, lancer TimoxVSSC et appliquer un pilote ASIO physique.

## Routage

Le routage se configure dans une matrice unique dont les axes sont choisis avec les menus de zone. Les sorties virtuelles peuvent être reliées aux sorties physiques ou aux entrées virtuelles. Les entrées physiques peuvent être reliées aux entrées virtuelles. Les canaux sont affichés par pages de 8, 16 ou 32, avec recherche indépendante pour chaque axe. Les barres de défilement larges et contrastées restent faciles à distinguer des cellules. Les canaux portant le repère `●` participent déjà à une route; la couleur aide à les localiser. La liste sous la matrice garde toutes les routes visibles et son bouton « Afficher dans la matrice » navigue jusqu’à la paire correspondante. Le gain et le mute restent réglables dans cette liste. Appliquer envoie la configuration complète par `configuration.apply`.

Les axes montrent uniquement les endpoints publiés par l’API. Pour faire apparaître les ports physiques, choisir puis appliquer un pilote ASIO physique. Pour faire apparaître les canaux TimoxVasio, ouvrir le pilote dans une application ASIO et activer ses canaux. Lorsqu’un axe est vide, l’interface indique l’action nécessaire.

Un changement de configuration interrompt le flux pendant la reconstruction du graphe. Le statut du moteur et les erreurs structurées sont affichés dans l’interface.

## MIDI

La section **MIDI** est séparée du son. Elle affiche le patch bay MIDI 1.0 du moteur : un bloc par port d'application et par appareil MIDI 1.0. Chaque application a son propre port : « <nom> In » (l'application envoie au patch bay, *from app*) et « <nom> Out » (le patch bay envoie à l'application, *to app*). Chaque sens s'active ou se désactive séparément.

- Tirer un câble d'une sortie vers une entrée crée une connexion MIDI. Le menu contextuel d'un câble ouvre ses propriétés (canaux, types de messages, canal de sortie, transposition) dans une fenêtre, ou le supprime. Un câble en pointillés est configuré mais inactif.
- Le menu contextuel du fond ajoute un port d'application ou un affichage BPM, ou relit la liste des ports.
- Le voyant vert d'un port indique de l'activité (notes, contrôleurs…), jamais les ticks d'horloge. Un port qui reçoit une horloge affiche son tempo (♩) ; s'il est barré, c'est probablement l'écho de l'horloge envoyée par le patch bay.
- **Horloge de référence** : l'horloge qui cadence le graphe audio, soit la carte ASIO, soit l'horloge logicielle sans carte. Tout le MIDI du moteur est placé sur ses échantillons, jamais sur les temporisateurs Windows. Ce n'est pas un rôle maître/esclave MIDI. L'en-tête de l'onglet indique sa source, sa fréquence réelle mesurée et son écart en ppm.
- **Horloge du moteur** : le bloc « Engine clock » génère une horloge MIDI en comptant les échantillons de l'horloge de référence ; elle ne dérive donc jamais de l'audio. Son menu contextuel la démarre, l'arrête, la relance (continue) ou règle son tempo dans une fenêtre. Le démarrage demande un audio en marche ; si l'horloge de référence s'arrête, le bloc affiche « Waiting for the reference clock ». On la relie par un câble comme n'importe quelle source.
- **Recalage** : dans les propriétés d'une connexion, « Reclock on the reference clock » réémet l'horloge reçue sur une grille régulière en échantillons de l'horloge de référence : la gigue disparaît, le tempo de la source est conservé. Le câble est alors plus épais.
- **Affichage BPM** : un widget à affecter à un port, depuis son menu contextuel ou en tirant un câble du port vers le widget. Il affiche le tempo de l'horloge qui arrive sur ce port, son état et le dernier start/stop. Le port d'arrivée identifie l'émetteur de l'horloge. Les widgets et la position des blocs restent dans l'interface : ils ne modifient pas le moteur.

## Performance

La vue « Performance » lit `bufferHealth` dans `GET /api/v1/state` : callback physique le plus lent, temps disponible pour un bloc, marge ou dépassement, démarrages retardés, dépassements de traitement et callbacks observés sur la dernière fenêtre de cinq secondes. La case « Enable stage measurements » envoie `performance.measurements.set` ; la collecte, désactivée au démarrage du moteur, échantillonne au plus un callback sur seize et publie la moyenne, le maximum et le nombre d’échantillons de chaque étape (appel de l’application, écriture dans l’anneau, attente, lecture des clients, routage, copie physique). Rien n’est ajusté automatiquement. Le [guide des fonctions](../docs/FONCTIONS.md) détaille la lecture de ces valeurs.

## API, journaux et cycle de vie du moteur

La navigation « API et Swagger » affiche la spécification OpenAPI et les commandes/événements WebSocket. Swagger UI, son style et les schémas OpenAPI sont empaquetés dans l’application; l’affichage ne dépend d’aucun CDN. Le descriptif WebSocket provient du champ `x-websocket` de `openapi-v1.json`.

La vue « Journaux » permet de choisir le niveau standard (`info`) ou détaillé (`debug`), d’actualiser les entrées et de démarrer ou arrêter le moteur. Le mode détaillé enregistre davantage d’événements de contrôle; aucun échantillon audio n’est écrit. Les entrées sont consultées par `GET /api/v1/diagnostics`; le niveau est enregistré par `PUT /api/v1/diagnostics`. Le journal se trouve dans `%LOCALAPPDATA%\TimoxVasio\logs\engine.log`, avec quatre archives au plus. Le niveau choisi est conservé dans `%LOCALAPPDATA%\TimoxVasio\diagnostics.json`.

L’arrêt passe par la commande WebSocket `engine.stop`. Il est refusé tant qu’un client ASIO est connecté. Quand Electron se ferme, le moteur reste actif et peut continuer de servir des applications audio; utilisez « Arrêter le moteur » lorsqu’aucun client n’est connecté.

## Lancement et développement

Voir `README.md` pour l’installation, le démarrage Electron et les commandes de développement. Le contrat se trouve dans `../schemas/api-v1.json` et `../openapi-v1.json`.
