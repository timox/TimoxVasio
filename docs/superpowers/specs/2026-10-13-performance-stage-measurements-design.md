# Mesures par étape dans l'onglet Performance — 1.2.1

**Français** | [English](2026-10-13-performance-stage-measurements-design.en.md)

## Objectif validé

Afficher dans l'onglet Performance des durées réellement observées aux étapes du trajet audio, sur des fenêtres de cinq secondes. Les chiffres doivent être rattachés à l'étape qu'ils décrivent, accompagnés du nombre d'échantillons et d'une explication courte. L'instrumentation supplémentaire est désactivée au démarrage et activable depuis l'interface. Aucun réglage automatique du tampon, de la phase ou du routage n'en découle. La livraison visée est la version 1.2.1.

## Points de mesure et sens des chiffres

1. **Application ASIO et sortie du pilote virtuel.** Dans la DLL, mesurer séparément le temps passé dans l'appel `bufferSwitch` ou `bufferSwitchTimeInfo` de l'application et le temps de copie de son bloc de sortie dans l'anneau partagé. Le premier chiffre inclut tout ce que l'hôte fait pendant cet appel ; il ne prétend pas isoler son DSP. Une mesure n'est publiée que si l'appel et l'écriture du bloc se terminent normalement.
2. **Attente dans l'anneau.** Associer au bloc écrit un numéro de première trame, sa taille, son époque audio et un horodatage QPC pris à la publication. À la lecture par le moteur, calculer l'intervalle seulement si la lecture correspond entièrement à ce bloc et à cette époque. Une lecture partielle, une sous-alimentation, un dépassement ou une reconfiguration écarte l'échantillon. Il s'agit du temps entre publication et lecture, pas d'une latence acoustique.
3. **Traitement du moteur et remise au pilote physique.** Dans le callback physique, mesurer séparément la lecture/désentrelacement des clients, l'exécution du graphe de routage, et la conversion/copie dans les tampons de sortie du pilote. Conserver le relevé existant du callback physique complet et son budget de période. La remise au tampon physique ne mesure ni la latence matérielle ni l'instant où le son atteint la sortie.

Ces durées sont présentées séparément. Elles ne sont pas additionnées pour produire une « latence totale » : les callbacks virtuel et physique s'exécutent de façon asynchrone, et une somme des maxima de fenêtre ne décrirait aucun bloc réel.

## Instrumentation à coût borné

Un interrupteur atomique lu par les chemins audio contrôle tous les nouveaux horodatages. Il est désactivé au démarrage du moteur et à chaque redémarrage. Hors activation, aucun nouvel appel QPC, aucune écriture de métadonnées de bloc et aucun calcul d'agrégat ne se produisent dans les callbacks ; seul subsiste le contrôle de l'interrupteur. Le relevé `bufferHealth` déjà présent reste inchangé.

Une fois activé, échantillonner au plus un callback sur seize par producteur. Les compteurs, sommes et maxima sont de taille fixe, préparés hors callback et publiés sans allocation, verrou, journalisation ni appel réseau depuis un thread audio. Les horodatages de l'anneau sont conservés dans des emplacements de taille fixe liés aux positions de trame ; le producteur écrit les métadonnées avant de publier son index, et le consommateur les vérifie avant de libérer l'espace. Le protocole de transport partagé passe de v8 à v9 : DLL et moteur 1.2.1 doivent être distribués ensemble, sans chemin de compatibilité ancien. L'interrupteur est publié dans le statut partagé par le moteur ; la DLL le lit sans interrogation réseau.

La tâche de contrôle consomme les agrégats par fenêtres complètes de cinq secondes. Elle réinitialise les résultats lors d'un arrêt, d'une désactivation, d'un changement d'époque audio ou d'une modification de configuration qui rend les échantillons incomparables. Chaque durée publiée comporte `samples`, `meanNs` et `worstNs`. Un résultat sans observation est `null`, jamais zéro. Les compteurs de blocs exclus sont publiés pour expliquer les lacunes de l'attente dans l'anneau.

## Contrat API

L'API documentée est modifiée avant ses producteurs et consommateurs : `API.md`, `openapi-v1.json`, `schemas/api-v1.json` et tests de contrat. La commande WebSocket `performance.measurements.set` reçoit exactement `{ "enabled": true | false }` et utilise le format de réponse de commande existant. Son état et la dernière fenêtre complète paraissent dans `GET /api/v1/state` sous `performanceMeasurements`. Cet objet contient toujours `enabled`, `sampleEveryCallbacks: 16` et `window`. Lorsque les mesures sont désactivées ou qu'aucune fenêtre complète n'existe, `window` vaut `null`.

Une fenêtre contient `durationMs: 5000`, `clients` et `physical`. Chaque entrée de `clients` porte `processId`, `driverId`, `hostCallback`, `ringWrite`, `ringWait` et `ringWaitDiscardedBlocks`. `physical` porte `clientRead`, `routing` et `outputCopy`. Chaque durée est soit `null`, soit `{ "samples": entier positif, "meanNs": entier positif ou nul, "worstNs": entier positif ou nul }`. Un client sans échantillon conserve son identité et une durée `null`. Les nombres sont en nanosecondes entières ; l'interface choisit µs ou ms pour la lecture. Le schéma refuse l'ancien format incomplet après introduction de ce contrat.

Les composants échangent ces données uniquement par le transport versionné et l'API documentée. L'interface n'inspecte ni mémoire partagée, ni fichier local, ni détail interne du moteur. La commande est idempotente : répéter la même valeur ne relance pas une session et ne duplique pas les compteurs.

## Présentation

Le haut de l'onglet conserve le budget du callback physique déjà mesuré. Immédiatement dessous, trois groupes suivent le trajet réel : application et copie virtuelle ; attente de bloc ; routage et copie physique. Chaque valeur affiche moyenne, maximum, échantillons et fenêtre. Le groupe moteur détaille les sous-étapes mesurées, sans masquer le chiffre du callback complet. L'interrupteur « Mesurer les étapes » et l'état de collecte apparaissent avant les groupes. Si la collecte est arrêtée, en cours de première fenêtre ou sans trajet actif, la vue l'indique et n'invente aucune valeur. Les explications restent au contact des chiffres.

## Vérification et livraison

Vérifier le contrat API et l'idempotence de la commande, l'appariement des blocs à travers le retour en début d'anneau, les lectures partielles et les changements d'époque. Vérifier par test ciblé que le mode désactivé n'exécute aucun nouvel horodatage, et comparer le coût des modes désactivé/activé sur un banc hors session audio réelle. Vérifier le rendu des états sans données et avec fenêtre complète. Les tests doivent valider le contrat cible, sans conserver l'ancien.

Avant toute validation avec processus audio, relever leur identité, commande complète, projet propriétaire et lien avec la session. L'autorisation de l'utilisateur permet d'arrêter les seuls processus pertinents après cette vérification. Recompiler la DLL, le moteur et l'interface ; redémarrer les processus concernés après changement runtime. Contrôler l'origine et les SHA-256 des binaires empaquetés. Préparer et publier la version 1.2.1 avec la DLL séparée et les exécutables Electron, documentation et notes de version mises à jour.
