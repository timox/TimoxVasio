# Avancement — plan 2026-10-21-wasapi-bridge.md

## Décisions

- Objectif : moteur commun, WASAPI partagé sans carte ASIO, pilote virtuel distinct.
- Premier incrément : interface d'hôte injectable, aucune modification de configuration publique.
- Branche de session existante ; aucun merge du main historique, installation ou arrêt de processus utilisateur.
- C++17 et aliases ASIO conservés ; le mode sans matériel appartient à l'incrément 2.
- Un agent possède contrôleur/hôte/CMake ; un second possède les tests de frontière. Revue indépendante ensuite.

## Préflight

| Interfaces partagées | Décision |
|---|---|
| Hôte/contrôleur | Fabrique worker, propriété COM inchangée, défaut ASIO. |
| Hôte/types publics | Aliases conservés, aucune migration JSON ici. |
| Runtime/callback | Format et retrait existants ; aucune seconde cadence. |
| Frontière/CMake DLL | Audit seulement, aucun backend ajouté. |
| Incréments 1/2 | Injection d'abord, politique sans carte ensuite. |

## Incrément 1

Terminé et revu, changements laissés dans l'arbre de travail sans commit. Les incréments 2 et 3 sont en cours ; les suivants ne sont pas encore livrés.

### Frontière DLL

Implémentée et revue. Trois faux négatifs trouvés puis corrigés : variables CMake sensibles à la casse, déclarations non analysables contenant des parenthèses, nom de cible transmis par variable. Le contrôle refuse les déclarations qu'il ne peut pas auditer.

Vérification par l'agent principal :

- `pwsh -NoProfile -File tests/wasapi_driver_boundary.Tests.ps1` : 13 cas passent.
- `pwsh -NoProfile -File tests/driver_import_policy.Tests.ps1` : passe.
- `pwsh -NoProfile -File tests/verify_driver_boundary.ps1` : verdict pass sur la DLL existante, non reconstruite ; SHA-256 `F4FC8AA0EDB929B74DB2459D6B69652955A1BE9FB21370C6A109DDB96983A0B6`.

Limite : audit statique conservateur des déclarations du fichier CMake pilote, pas interprétation générale de CMake ni audit de chargement dynamique.

### Découplage du contrôleur

Interface/fabrique en place. Revue initiale : aucune allocation/verrou supplémentaire dans les callbacks ; course trouvée dans la publication d'un échec au démarrage (événement signalé avant started=false).

La course a été reproduite par le test de fabrique levant une exception, puis corrigée en publiant started=false avant l'événement. Le test vérifie aussi thread worker distinct de l'appelant, durée de vie de l'hôte, stop/close, négociation, quantum invalide et fréquence non confirmée. Il teste le contrôle/découverte, pas un callback audio sans clients.

`build_codex_110/Release/AudioControllerClockTests.exe` : PASS, y compris 50 échecs de fabrique successifs, réexécuté par l'agent principal. Revue indépendante finale : approuvée, aucun point important restant.

### Compilation et régressions finales

Commande de build (code retour 0) :

```powershell
cmake --build build_codex_110 --config Release --target AudioControllerClockTests TimoxVirtualAsioEngine AudioControllerPersistenceTests ApplicationProfilesApiTests AudioRoutingRuntimeTests AudioRoutingHotSwapTests RealtimeCallbackSlotTests PerformanceStageMeasurementsTests
```

Les sept exécutables de tests ci-dessus ont ensuite été exécutés par l'agent principal : tous code 0. Ils couvrent contrôle/découverte, persistance, API locale, transfert audio du runtime, permutation du graphe, retraite du callback et mesures d'étapes.

Moteur compilé : `build_codex_110/Release/TimoxVirtualAsioEngine.exe`, 1016832 octets, horodatage UTC `2026-10-21T17:45:17.2285996Z`, SHA-256 `BCFE5B33565AE5E3215E1137C02808CBE4B1F17ACB23B08B157BB244645E5B07`. Il n'a pas été installé ni lancé à la place d'un moteur utilisateur. La DLL n'a pas été reconstruite/modifiée.

`git diff --check` : code 0, seuls avertissements Git de normalisation LF/CRLF. Tests matériels ASIO/WASAPI non exécutés ; aucun nouveau backend ou fonctionnement sans matériel livré dans cet incrément.

## Exigence complémentaire du mainteneur

Le contrôle graphique, l'accès API et les performances doivent atteindre au moins le niveau de TimoxVasio. Control devient l'interface unifiée. La qualification devra comparer les chemins audio et les distributions de temps de callback ; les tests unitaires seuls ne prouvent pas cette parité. L'autorisation d'interrompre les processus audio du projet et d'élever les privilèges a été donnée explicitement ; identifier les processus avant toute interruption.

## Incrément 2 — Implémenté, qualification en cours

- Hôte logiciel : deadlines QPC absolues, timer annulable, MMCSS, arrêt avec jonction ; erreurs du worker remontées au contrôleur sans redémarrage implicite.
- Configuration explicite `clockSource`, persistance compatible avec les anciens fichiers ; source omise conservée dans la transaction. Le graphe logiciel démarre sans carte ni routes.
- Routes matérielles indisponibles conservées dans la configuration et désactivées dans le graphe logiciel. Test de transport audio entre deux mappings clients synthétiques.
- API, schéma et documentation FR/EN adaptés ; Control expose la source et son format dans le contenu principal.
- Tests natifs SoftwareClockHostTests, AudioControllerClockTests, SoftwareClockRoutingTests et AudioConfigurationStoreTests exécutés avec succès. Tests de contrat PowerShell et adaptateur Node réussis. React : 9 suites, 57 tests réussis après correction du statut d'un graphe logiciel vide et de la restauration. Build React réussi avant le dernier renforcement de restauration.
- Revue indépendante : validation avant arrêt de l'ancien moteur, conservation des routes dormantes et propagation des erreurs corrigées. Restauration GUI software→ASIO avec canaux manquants corrigée ; la phase d'ouverture conserve les routes anciennes pour qu'elles restent connues du validateur API.
- ApplicationProfilesApiTests reconstruit et exécuté avec succès : route de la carte absente A, ouverture du pilote factice B à un canal, puis restauration logicielle à 96000 Hz/128 frames avec routes exactes. Aucun matériel requis. AudioControllerPersistenceTests, AudioRoutingRuntimeTests et AudioRoutingHotSwapTests également réussis après reconstruction.
- Pas encore de preuve avec applications ASIO réelles, ni qualification longue de cadence ou comparaison de performances. Pas de packaging/installation.

## Incrément 3 — En cours

- Anneau SPSC préalloué avec lectures/écritures partielles, tests de concurrence et absence d'allocation ; SpscAudioRingTests réussi, revue indépendante sans blocage.
- SRC sinc polyphasé et PI implémentés, revus sans blocage. CTest pur initial : 2/2 réussi, réexécuté par le parent. SNR sur signaux analytiques : 132,65–145,72 dB ; rejet d'alias sur quatre fréquences et corrections ±0,2 % : 112,65–131,34 dB. Découpage exact, vidage fractionné après ratio variable, anti-windup et absence d'allocation testés. Ces résultats ne qualifient pas le coût CPU : FIR scalaire 256 taps, table d'environ 4 Mio par instance.
- Correction du plan : avec ratio sortie/entrée et le FIFO défini, le retour sur remplissage est négatif dans les deux directions. Pour la même dérive matérielle, les corrections stabilisées capture/rendu sont opposées.
- CMake propose un mode de tests DSP purs indépendant du SDK Windows. WasapiBridgeIo possède un format immuable, un FIFO par génération, des transferts bornés et le silence des lectures partielles ; tests et revue réussis. La publication/retraite de ces générations dans le graphe reste à intégrer.

## Incrément 4 — Premières briques

- Énumérateur WASAPI et parsing borné des formats implémentés, revue indépendante favorable. Tests déterministes compilés/exécutés par le parent. Sonde séparée de l'agent : 132 endpoints, 22 actifs avec formats, six résolutions de rôles et d'IDs réussies ; aucun flux démarré.
- Backend injectable capture/rendu/loopback endpoint partagé implémenté avec IAudioClient, format natif, événement annulable et possession des paquets. Revue corrigée : événement installé immédiatement après Initialize ; Start/Stop refusent un paquet encore acquis. Reconstruction canonique et tests déterministes finaux réussis.
- Sonde d'ouverture/fermeture de l'agent : 16 ouvertures rendu/loopback réussies ; les 14 essais capture retournent E_ACCESSDENIED, y compris hors sandbox. Ce résultat ne prouve pas un flux audio fonctionnel. Aucun Start ni lecture audio pendant cette sonde.
- Conversion PCM, worker adaptatif, reconnexion, activation loopback processus et raccordement au graphe/API/GUI restent à implémenter. TimoxVasio.dll reste inchangée.

## Dernière vérification

- CTest pur complet : 3/3 réussi (anneau, SRC renforcé, IO pont avec niveau de remplissage).
- Tests natifs backend/énumérateur/IO pont réussis ; build React final réussi après correction de restauration.
- Moteur canonique : 1034752 octets, UTC `2026-10-21T21:10:24.9034828Z`, SHA-256 `3F72EBBFAB1B57D2998D0DC0E348F3D8606E679938B77A8CE3ABE943EEE8A753`. Les briques backend sont pour l'instant compilées dans leurs tests, pas encore raccordées au moteur.
- DLL canonique toujours SHA-256 `F4FC8AA0EDB929B74DB2459D6B69652955A1BE9FB21370C6A109DDB96983A0B6`. Aucun packaging, installation ou remplacement du moteur utilisateur effectué.

## Suite — flux, runtime et documentation

- Conversion native PCM8/16/24/32 et float32/64, bits valides, non-finis, saturation et accès non alignés implémentés et revus. Maximum positif ajouté à la couverture. CTest pur complet : 4/4 réussi.
- Runtime : bindings WASAPI possédés hors callback, capture avant graphe/rendu après, rejet boucle endpoint-loopback vers même rendu résolu. WasapiRoutingRuntimeTests réussi, y compris transport AudioClientMapping réel isolé, canaux clairsemés, gain, silence, compteurs en trames et absence d'allocation. Régressions AudioRoutingRuntimeTests, RoutingGraphTests et AudioRoutingHotSwapTests réussies. Les compteurs historiques des mètres ne représentent pas les pertes WASAPI ; exposer Health séparément reste nécessaire.
- Worker WasapiBridge : préallocation, COM/MMCSS/FTZ sur worker, SRC adaptatif, fragments conservés, préremplissage, erreurs et arrêt annulable. Tests canoniques WasapiBridgeTests réussis. Revue indépendante complète du worker et sonde streaming réelle encore requises. Une instance correspond à une génération ; reconnexion et publication contrôleur non raccordées.
- Persistance : wasapiBridges optionnel, sélecteurs stricts, UTF-8/UTF-16 borné, compatibilité legacy. AudioConfigurationStoreTests réussi après correction de Save qui pouvait écrire une horloge logicielle invalide. Contrôleur/API ne pilotent pas encore ces configurations.
- Benchmark ASIO antérieur conservé sous build_wasapi_baseline/AudioPathBenchmark-before.exe, SHA-256 `47B3A5256DE2E326E9776C067B695018D5B0400084D47102AC538B1C75371102`. Nouvelle cible AudioPathBenchmark compilée, aucune comparaison chronométrée encore réalisée. La copie antérieure seule ne prouve pas une provenance source exacte.
- À la demande du mainteneur : [guide illustré français](../../ASIO_WASAPI_GUIDE.md) et traduction anglaise, deux schémas, sens des câbles, rôle du futur pilote Windows, contraintes de dérive/latence/CPU/récupération et état honnête des livraisons. Revue des flux effectuée ; liens ajoutés aux pages Architecture.
- Le moteur canonique doit être reconstruit après les dernières modifications runtime/store avant toute sonde utilisateur ; le hash précédent décrit le build précédent, pas l'intégration complète courante.

## Point intermédiaire du 22 octobre — intégration

- Contrôleur et gestionnaire de ponts raccordés au runtime : générations, reconnexion avec délai progressif, suivi du périphérique par défaut et conservation des routes indisponibles. Tests isolés du contrôleur réussis ; reconstruction canonique en cours au moment de ce point.
- API 1.1 et Control raccordés aux configurations et états des ponts. Compatibilité de lecture 1.0 conservée. Validation globale et qualification graphique encore en cours.
- Sonde réelle rendu/loopback réussie : voir [rapport de streaming](2026-10-21-wasapi-stream-smoke.md). Cela ne qualifie ni la capture microphone, ni les applications ASIO réelles, ni les performances prolongées.
- Comparaison initiale des benchmarks trop variable pour conclure à la parité ; comparaison depuis une référence source contrôlée encore nécessaire.
- Commits intermédiaires demandés par le mainteneur : ils constituent une sauvegarde révisable du développement, pas une release. Pilote Windows virtuel natif, qualification longue et packaging restent à réaliser. TimoxVasio.dll conserve sa frontière indépendante.

### Vérification avant commits intermédiaires

Reconstruction canonique réussie de `TimoxVirtualAsioEngine`, `AudioControllerWasapiTests` et `WasapiBridgeApiTests`. Ces deux exécutables de tests ont ensuite terminé avec le code 0. CTest DSP pur : 4/4 ; contrats PowerShell API et WASAPI réussis ; frontière du pilote : 13 cas réussis. React : 9 suites, 63 tests ; adaptateur de contrat Node : 7 tests ; client Electron : 10 tests. `git diff --check` réussi. Cette vérification ne remplace pas la qualification matérielle, graphique et de performance encore ouverte ci-dessus.

## Qualification du 22 octobre

- [Vrai trajet ASIO→ASIO sans carte](2026-10-22-real-asio-software-clock.md) : DLL inchangée chargée dans deux processus, 48 kHz/2048 frames, cinq secondes analysées, SNR 129,56 dB. Control/moteur installés rétablis, configuration utilisateur inchangée et état sain confirmé. Ce résultat ne qualifie pas encore les ponts WASAPI ni dix minutes de stabilité.
- [Comparaison de performances contrôlée par sources](2026-10-22-audio-path-comparison.md) : quatre exécutions réussies mais variabilité persistante ; aucune conclusion de parité.
- API : quatre réponses HTTP réelles du test natif (initial, ponts désactivés, suppression, inventaire) exportées et validées par `Test-Json`, en plus des fixtures. Pas de preuve de flux matériel dans ce test.
- GUI : modale vérifiée à 768×800 et graphe à 1440×1000 avec données simulées ; checkbox corrigée, durée présentée en millisecondes en conservant la précision du contrat 100 ns. Suite React complète : dix suites, 69 tests réussis. Voir le [rapport visuel](2026-10-22-wasapi-gui-visual-check.md).
- [Pilote Windows natif](../specs/2026-10-22-native-windows-audio-driver.md) : conception PortCls/WaveRT et premier contrat de paquets portable `native-windows-audio/`. Compilation C/C++ réussie, CTest 1/1 réussi et revue indépendante sans blocage. Ce lot ne fournit encore aucun endpoint, IOCTL ou pilote noyau ; outillage WDK à compléter.

## Qualification réelle et outillage — suite du 22 octobre

- [Trajet ASIO→WASAPI→loopback→ASIO](2026-10-22-real-asio-wasapi-loopback.md) : un premier essai a révélé que le graphe consommait la capture avant son préremplissage. Correctif et tests déterministes publiés dans `9e96d44` ; contre-essai matériel réussi sur 480000 frames, 400/400 fenêtres, 9,0212345 secondes de stabilité observée après démarrage. Les compteurs de démarrage sont conservés, pas remis à zéro. La stabilité prolongée et la modulation du ratio restent à qualifier.
- Banc de mesure et contrôles négatifs publiés dans `fea0449` : analyse de présence des deux tons, progression des compteurs natifs, absence de nouveaux incidents, générations inchangées et intervalle maximal entre observations. Tests de l'analyseur : 24 cas ; tests du contrôle de stabilité : 25 cas. Aucune prétention de mesure de latence ou de fidélité numérique dans ce mode.
- Chaîne WDK publiée dans `e1846ec` : [acquisition épinglée et scripts](../../../native-windows-audio/BUILDING.md), compilation réelle du SysVAD Microsoft avec contrôles INF/API, code 0. Hash du binaire vérifié indépendamment. Il s'agit d'un pilote de référence non signé, pas du pilote Timox ni d'un paquet installé. L'adaptation des endpoints natifs démarre après cette preuve.
- Ces trois commits intermédiaires ont été poussés sur `ccr-d306084d-ieaii9`. Le moteur et la DLL installés n'ont pas été remplacés ; les essais utilisent le moteur de développement et une configuration isolée avec restauration.
