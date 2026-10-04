# Guide d’utilisation de TimoxVasio Control

## Configuration audio

L’interface présente les pilotes ASIO physiques découverts par le moteur et l’unique pilote virtuel `TimoxVasio`. Sélectionner le pilote physique, puis choisir une fréquence et une taille de buffer parmi les capacités qu’il annonce. La même fréquence et la même taille sont utilisées par le moteur et les clients TimoxVasio.

L’inventaire API présente les applications connectées avec leur PID, leur nom et les canaux réellement alloués par chacune. Les endpoints virtuels disponibles correspondent à ces canaux actifs. Les ports physiques sont ceux retournés par le pilote après application de la fréquence.

## Sélection dans une application audio

Dans un hôte ASIO, choisir l’API son ASIO puis le périphérique `TimoxVasio`. Après l’installation du pilote, fermer puis relancer l’hôte pour renouveler sa liste. Mixxx 2.6 beta x64 retire actuellement ce pilote de sa liste parce qu’il annonce 256 entrées et sorties; voir [l’analyse de compatibilité Mixxx](../docs/mixxx-256-channel-compatibility.md). `TimoxVasio` reste visible pour la découverte même si le moteur n’est pas prêt; pour ouvrir un flux audio, lancer TimoxVasio Control et appliquer un pilote ASIO physique.

## Routage

Le routage se configure dans une matrice unique dont les axes sont choisis avec les menus de zone. Les sorties virtuelles peuvent être reliées aux sorties physiques ou aux entrées virtuelles. Les entrées physiques peuvent être reliées aux entrées virtuelles. Les canaux sont affichés par pages de 8, 16 ou 32, avec recherche indépendante pour chaque axe. Les barres de défilement larges et contrastées restent faciles à distinguer des cellules. Les canaux portant le repère `●` participent déjà à une route; la couleur aide à les localiser. La liste sous la matrice garde toutes les routes visibles et son bouton « Afficher dans la matrice » navigue jusqu’à la paire correspondante. Le gain et le mute restent réglables dans cette liste. Appliquer envoie la configuration complète par `configuration.apply`.

Les axes montrent uniquement les endpoints publiés par l’API. Pour faire apparaître les ports physiques, choisir puis appliquer un pilote ASIO physique. Pour faire apparaître les canaux TimoxVasio, ouvrir le pilote dans une application ASIO et activer ses canaux. Lorsqu’un axe est vide, l’interface indique l’action nécessaire.

Un changement de configuration interrompt le flux pendant la reconstruction du graphe. Le statut du moteur et les erreurs structurées sont affichés dans l’interface.

## Lancement et développement

Voir `README.md` pour l’installation, le démarrage Electron et les commandes de développement. Le contrat se trouve dans `../schemas/api-v1.json` et `../openapi-v1.json`.
