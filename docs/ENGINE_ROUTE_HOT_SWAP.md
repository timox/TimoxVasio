# Mise à jour des routes sans interrompre l'horloge ASIO

**Français** | [English](ENGINE_ROUTE_HOT_SWAP.en.md)

## Constat

Le mode `multiclient` du 9 octobre 2026 a mesuré des pauses communes de
219 ms à l'arrêt d'un client et de 318 ms à son retour, dans
`tests/results/multiclient-20261009-151756-085-548ce1c8/`.
Les positions d'échantillons des clients survivants sont restées continues.
Le contrôleur appelle actuellement `ApplyConfigurationCore` quand les routes
résolues changent ; cette fonction arrête et rouvre la carte physique avant
de reconstruire le graphe.

## Chemin prévu

Quand le pilote physique, la fréquence et le buffer restent identiques et que
les voies physiques requises par les nouvelles routes font partie des voies
déjà ouvertes, le contrôleur construit un nouveau `AudioRoutingRuntime` sur
son thread de travail. Il publie ensuite son pointeur atomiquement pour le
callback physique. Le callback ne fait qu'un chargement atomique et traite le
graphe sélectionné. Le contrôleur attend la fin des callbacks déjà engagés
avant de détruire l'ancien graphe. Aucun verrou, allocation ou appel système
supplémentaire n'est introduit dans le callback.

Si le format change, si une nouvelle voie physique doit être ouverte ou si
aucune route ne reste active, le chemin existant de réouverture de la carte
reste utilisé. Les voies physiques ouvertes au démarrage forment donc un
surensemble autorisé pour les remplacements à chaud ; une route ne peut pas
utiliser un buffer ASIO physique qui n'a jamais été créé.

## Vérification

Tester le passage atomique et la durée de vie de l'ancien contexte dans un
test de concurrence isolé. `RealtimeCallbackSlotTests` vérifie que l'ancien
contexte reste vivant jusqu'à la fin du callback ; `AudioRoutingHotSwapTests`
fait passer deux graphes différents par le même point d'appel, avec des
échantillons de sortie distincts. Les deux tests passent en Release x64.
Refaire ensuite le mode 5 à 2, 4 et 8 clients avec
les mêmes seuils. Les journaux de cadence des clients survivants doivent
montrer la disparition des pauses communes lors des fermetures et ouvertures.
Le moteur de développement actuellement utilisé par Mixxx et SuperCollider
ne sera pas remplacé pendant leur session.
