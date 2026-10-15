# Vérification de TimoxVasio dans des hôtes réels

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../README.md) et la documentation courante qu'il référence.

Cette fiche complète les mesures automatiques de `vasio-probe`. Noter la date, la version du pilote et du moteur, le modèle de carte physique, son pilote, Windows, la fréquence, le buffer et les routes actives. Conserver les journaux du moteur, une capture de la configuration et le rapport LatencyMon avec chaque session.

Avant tout essai, sauvegarder `%LOCALAPPDATA%\TimoxVasio\configuration.json` et `%LOCALAPPDATA%\TimoxVasio\application-profiles.json`. Fermer les autres hôtes ASIO. Ne changer qu'un paramètre à la fois. Un test est « non exécuté » tant que sa durée complète et ses observations ne sont pas consignées.

## Renoise

1. Ouvrir **Preferences → Audio** et choisir `TimoxVasio`. Noter si le pilote apparaît, les noms des voies d'entrée et de sortie, les fréquences et tailles de buffer proposées.
2. Essayer chaque fréquence/buffer retenu dans la matrice automatique. Noter la valeur réellement confirmée par `GET /api/v1/state`, puis si Renoise démarre, émet du son et reçoit du son.
3. Lire un projet avec transitoires et silence pendant **30 min**. Lancer LatencyMon en parallèle et relever l'heure de chaque craquement, arrêt ou erreur. Comparer avec les compteurs de buffer dans **Logs → Physical ASIO buffer** et les journaux du moteur.
4. Fermer puis rouvrir Renoise pendant qu'un autre client TimoxVasio reste ouvert. Noter le temps de reconnexion, les voies retrouvées et tout décrochage de l'autre client.

## Mixxx

1. Dans **Preferences → Sound Hardware**, choisir `TimoxVasio`. Noter la présence du pilote, les noms et numéros de voies, les fréquences et buffers disponibles.
2. Assigner au moins une paire Master et une paire Cue à des voies distinctes. Confirmer la fréquence et le buffer réellement actifs dans l'état du moteur.
3. Lire deux pistes avec transitions pendant **30 min**, LatencyMon actif. Consigner les craquements, les voies silencieuses et les changements de buffer ou de fréquence.
4. Fermer et rouvrir Mixxx pendant que Renoise ou SuperCollider utilise TimoxVasio. Vérifier que l'autre client continue et que les affectations reviennent.

## SuperCollider

1. Configurer `Server.default.options.device` pour `TimoxVasio`, puis noter les voies exposées, les noms, la fréquence et la taille de bloc acceptées par le serveur.
2. Démarrer `scsynth` et jouer un signal continu plus des impulsions distinctes pendant **30 min**, avec LatencyMon. Relever toute erreur de démarrage, perte de signal ou désynchronisation.
3. Redémarrer `scsynth` pendant qu'un autre client tourne. Vérifier les voies et l'absence d'effet sur le client restant.

## Chaîne Mixxx → TimoxVasio → SuperCollider

1. Sauvegarder la configuration actuelle. Router deux sorties Mixxx vers deux entrées SuperCollider à gain 0 dB, sans autre route vers ces entrées.
2. Jouer un signal identifiable dans Mixxx et contrôler séparément gauche et droite dans SuperCollider, puis à l'oreille sur la sortie physique.
3. Pendant que Mixxx et SuperCollider jouent, lancer d'abord `pwsh -NoProfile -File tests/with-loopback-route.ps1 -AllowOtherClients -PlanOnly` pour vérifier qu'une voie de probe est libre, puis relancer sans `-PlanOnly` avec `-Seconds 300` (ou `1800` pour une observation longue). Le script ajoute temporairement une route à 0 dB entre un probe émetteur et un probe récepteur, enregistre `vasio-status`, mesure l'identité et la latence, puis restaure routes et profils. Ne modifier aucune autre route pendant ce contrôle, car le script restaure l'instantané initial. Son application de configuration peut brièvement interrompre le son ; noter cet événement séparément. Le signal codé prouve l'intégrité du transport virtuel pendant que les deux hôtes tournent, pas l'identité bit à bit de la piste Mixxx. Celle-ci se vérifie à l'écoute et par une capture dédiée si nécessaire.
4. Répéter avec démarrage de SuperCollider avant Mixxx, puis dans l'ordre inverse. Arrêter chaque hôte une fois pendant que l'autre reste actif. Restaurer la configuration sauvegardée.

## Fiche de résultat à remplir pour chaque combinaison

| Champ | Valeur |
| --- | --- |
| Date et hôte/version | |
| Pilote TimoxVasio et moteur (chemin, version, SHA-256) | |
| Carte physique/pilote/version | |
| Fréquence et buffer demandés/réels | |
| Voies proposées/utilisées | |
| Durée de lecture et nombre d'incidents audibles | |
| Retards et dépassements du buffer physique | |
| Rapport LatencyMon | |
| Fermeture/réouverture, effet sur les autres clients | |
| Verdict et reproduction minimale d'un défaut | |
