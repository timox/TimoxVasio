# Transport audio compact par client — conception 1.1.1-test

> **ARCHIVE** — Document historique conservé pour référence. Il ne décrit pas l'état courant de TimoxVasio (1.2.1). Voir le [README](../../README.md) et la documentation courante qu'il référence.

## Objectif

Réduire les copies des anneaux lorsque l’hôte ASIO crée moins de 256 voies, sans changer la capacité annoncée de 256 voies par sens, la cadence des callbacks, le tampon ni la phase. La configuration des voies est établie hors callback dans `createBuffers` et reste immuable pendant une génération audio.

## Contrat cible

Le contrat privé DLL–moteur passe à v8. Un petit objet de contrôle stable identifie le client par pilote, PID et identité de création du processus ; il publie l’état, le format et la génération audio courante. Chaque `createBuffers` crée un objet audio distinct portant cette génération. Le moteur ouvre uniquement la génération publiée après vérification du magic, de la version, de la taille, de l’identité, des strides et des listes de voies. Une version inconnue est rejetée explicitement ; aucun décodage audio v6/v7 n’est conservé dans le contrat cible.

Pour chaque sens, la liste contient les numéros ASIO créés, uniques et triés, de 0 à 255. Le stride en floats par trame est la longueur de cette liste, indépendamment des routes. Les index des deux anneaux restent en trames ; chaque génération commence avec des index à zéro. Le cas zéro voie conserve la progression des trames et le signal au client. La conversion ASIO ↔ position compacte utilise une table inverse préparée hors callback.

La première implémentation peut conserver la capacité mémoire maximale des anneaux v8 tout en copiant seulement `frames × stride` ; elle ne revendique alors aucun gain de mémoire. Une allocation à la taille du stride est une évolution séparée, après validation du contrat et des mesures.

## Publication et durée de vie

Le thread de la DLL est arrêté et rejoint avant `createBuffers` ou `disposeBuffers`. La DLL construit et initialise une nouvelle génération hors callback, puis la publie atomiquement dans le contrôle. Elle ne réécrit jamais le stride ou les échantillons d’une ancienne génération. Le moteur détecte aussi les changements de génération des clients déjà connus, construit un runtime correspondant hors callback et publie celui-ci via le relais existant. L’ancien runtime et son mapping restent vivants jusqu’à la quiescence des callbacks physiques. Le démarrage du callback DLL exige l’acquittement de la génération et du format par le moteur ; en absence d’acquittement, il échoue explicitement hors callback. Aucun callback n’attend une bascule.

Un client attaché sans route continue de recevoir ses échanges et son signal à chaque cycle du callback physique. L’hôte physique garantit le silence quand aucun runtime n’est publié. Les sous-alimentations remplissent le bloc demandé avec du silence selon le stride de la génération.

## API et documentation

L’API HTTP/WebSocket reste le passage obligatoire pour UI et outils. `API.md` doit distinguer la capacité ASIO de 256 voies du nombre de slots réellement transportés, et retirer l’affirmation selon laquelle chaque client possède toujours 256 slots physiques dans les anneaux. Les nouveaux états observables éventuels sont définis et testés dans l’API avant consommation par l’UI. Aucun lecteur UI ne lit le mapping privé.

## Vérification ciblée

Compiler ensemble la DLL et le moteur Release. Adapter les probes natifs pour 0, 2, 8 et 256 voies par sens, voies clairsemées dont 256, sens asymétriques, retour d’anneau, sous-alimentation, dépassement, client sans route, génération remplacée et rejet d’un format ancien. Vérifier identité des échantillons et ordre des canaux. Comparer ensuite seulement les mesures de transport à routage et charge identiques avec `docs/MESURES_1_1_1_TEST_2026-10-11.md` ; ce banc hors ligne ne prouve pas un gain dans le callback ASIO réel. Ne pas lancer de charge ni remplacer les binaires des sessions audio actives.

## Risques et limites

La quiescence du contrôleur protège le callback physique, mais ne prouve pas seule la fin du thread de la DLL. Le `stop` suivi d’un `join` effectif est nécessaire côté DLL. Une génération distincte évite toute réinterprétation d’anciens blocs pendant cette transition. Les deux générations peuvent coexister temporairement en mémoire ; la taille maximale des deux anneaux actuels est d’environ 32 Mio par génération à 256 voies dans les deux sens. Les gains de temps réel restent à mesurer.
