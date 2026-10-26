# Suivi — plan : docs/plans/2026-10-26-repartition-depots.md

## Autorisation et méthode

L'utilisateur a confié au parent la gestion des sous-agents et la bonne
exécution du plan. L'exécution commence par des imports isolés et vérifiables.
Le parent reste responsable des contrats partagés et de la revue des résultats.
Pas de publication, suppression des sources originales ou essai audio implicite.

## Décisions d'exécution

- Ruling : préparer les trois imports sous `build_migration_20261026/` dans
  la racine autorisée, puis les transférer aux dépôts cibles après revue et
  autorisation technique d'écriture hors sandbox. Cela préserve les sources
  locales ; cet espace ignoré par Git ne doit pas être nettoyé avant transfert.
- Ruling : WASAPI reste module lors de la première extraction, sans décision
  définitive contre un dépôt bibliothèque. Explication conservée dans
  `REPRISE_2026-10-26.md` à la demande de l'utilisateur.
- Ruling : conserver le protocole API et les responsabilités actuelles pendant
  l'extraction. Les décisions métier ouvertes du modèle ne sont pas résolues
  implicitement par le déplacement des sources.
- Ruling : un snapshot du contrat partagé pilote/moteur peut servir de
  dépendance vendue avec commit et SHA-256, sans modifications divergentes.
- Ruling : corriger séparément le défaut confirmé de validation de version du
  superviseur PWAR dans l'import moteur, avec test de régression.

## Préservation

Snapshot vérifié de 1 149 fichiers suivis et non suivis non ignorés, HEAD source
`3bcc05f5fed34ea8404e478ef34bb230a2efe39c`.

- Sources : `build_migration_20261026/source-snapshot/`.
- Empreintes SHA-256 : `build_migration_20261026/source-snapshot.json`.
- État Git initial : `build_migration_20261026/source-status.txt`.
- Chaque copie a été comparée à son fichier source par SHA-256.
- Les sources PWAR ignorées sous les anciens dossiers de build nécessitent
  leur propre manifeste dans le lot PWAR ; elles ne sont pas couvertes par
  ce snapshot Git.
- La clarification WASAPI et le présent suivi sont postérieurs au snapshot.

## Interfaces entre lots

| Producteur → consommateur | Contrat | Traitement |
|---|---|---|
| Pilote → moteur | Transport et disposition mémoire partagés | Snapshot inchangé, provenance et tests |
| Moteur → GUI | Exécutable, API, schémas | Artefacts épinglés, tests de contrat |
| PWAR → moteur | Protocole JSON v1 et binaire identifié | Validation version et hash maintenues |
| Moteur/PWAR → packaging GUI | Chemins et SHA-256 | Import explicite, erreur si manquant |
| WASAPI → moteur | Buffers et callbacks | Couplage conservé pendant l'extraction |

## Lots en cours

| Lot | Responsable | État | Preuve attendue |
|---|---|---|---|
| Préservation et manifeste | Parent | Effectué | Snapshot et manifeste dans chaque dépôt |
| Import moteur | `extract_engine` | Transféré et commité localement | Build réussi ; 30/30 tests verts |
| Import GUI | `extract_gui` | Transféré et commité localement | 34 tests verts ; build React et packaging répertoire réussis |
| Import PWAR | `extract_pwar` | Transféré et commité localement | Build Windows et 21 tests hors réseau verts |
| Revue et transfert | Parent + `review_imports` | Effectués | 370 fichiers vérifiés après copie ; octets Git vérifiés après correction des attributs |
| Refonte métier/API | Parent | Non commencée | Décisions du modèle validées |
| Qualification audio/Linux | Parent + PWAR | Non commencée | Créneau coordonné et résultats réels |

## Limites constatées

L'archive upstream PWAR référencée par l'ancien bundle manque localement.
L'import peut préserver et identifier les sources consolidées réelles ; il
ne peut pas prétendre avoir rejoué l'intégralité des patches depuis cette
archive. Le contrôle Linux et les essais audio restent distincts.

## Vérifications intermédiaires

- Parent : 92 empreintes source/cible GUI vérifiées ; manifeste livré avec
  93 entrées, dont son entrée propre sans hash récursif.
- Parent : 34 tests Node/Electron/contrat GUI relancés, tous verts.
- GUI : 35 tests React passent, 38 échouent ; exactement le même bilan sur
  la copie originale isolée. Aucun correctif sans rapport ajouté. Ces échecs
  restent ouverts ; la migration ne transforme pas cette suite en suite verte.
- Parent : 111 empreintes source/cible PWAR vérifiées ; 21 tests hors réseau
  relancés via CTest, tous verts.
- PWAR : client Windows identifié par SHA-256
  `29D5E3208E10AE5FEBBB7FD3914790A87EAF12D32BE6BEF923F20D7E33566FD3`.
- Revue indépendante `review_imports` : GUI/PWAR et script de transfert
  examinés ; absence de blocage d'import identifiée à ce stade. Revue moteur
  et vérification des artefacts appariés encore en cours.
- Script de transfert préparé et vérifié syntaxiquement. Il contrôle les
  chemins, refuse traversées et liens de répertoire, vérifie les empreintes,
  exige des dépôts cibles propres et crée des branches de migration. Il
  sauvegarde les fichiers existants explicitement attendus avant remplacement.
  Le transfert a ensuite été effectué et vérifié, sans suppression des sources.

## Résultat de l'extraction

Les trois dépôts voisins sont sur `migration/extraction-20261026`. Commits
initiaux locaux : moteur `9adce07`, GUI `8351a6b`, PWAR `7692425`. Aucun push.

Commits finaux après conservation des octets : moteur **`1798297`**, GUI
**`cf388c4`**, PWAR **`5a0f13e`**. Le contrôle direct des blobs commités et
des fichiers de travail passe pour 159 fichiers moteur, 93 fichiers GUI et
112 fichiers PWAR ; les entrées propres des manifests ne sont pas auto-hashées.

Le parent a exécuté la suite moteur finale : 30/30 tests réussis en 52,32 s,
y compris les deux API locales isolées. Corrections distinctes documentées :
validation de version PWAR ; rejet d'une configuration WASAPI sans sélecteur
d'endpoint ; corrélation par identifiant dans le test WebSocket au lieu d'un
nombre fixe d'événements initiaux.

Le packaging répertoire GUI a été construit sans lancement de l'application.
Les copies embarquées du moteur et du client PWAR correspondent aux builds ;
les schémas embarqués correspondent aux sources épinglées. Artefact de travail :
`build_migration_20261026/TimoxVSSC/dist/win-unpacked/TimoxVSSC.exe`.

La comparaison du snapshot initial confirme qu'aucun fichier de code original
de TimoxVasio n'a changé. Seule la note demandée dans la reprise diffère parmi
les fichiers préexistants ; les nouveaux documents de planification sont séparés.

Ruling : désactiver la conversion automatique des fins de ligne dans les
nouveaux dépôts. Une vérification des blobs Git a reproduit un écart SHA-256
sur OpenAPI malgré un fichier de travail conforme. Les manifests et les
attributs Git doivent préserver les octets importés sur les clones futurs,
sans modifier le code exécuté ou les binaires déjà testés.

## Reste du plan

- La première extraction est distincte de la refonte du modèle/API : cette
  dernière n'a pas été entreprise, les décisions métier restant ouvertes.
- WASAPI reste provisoirement dans le moteur ; le dépôt bibliothèque reste
  une option ouverte, conformément à l'explication approuvée et consignée.
- Ne pas supprimer les anciennes copies avant la bascule complète des usages.
- Linux/PipeWire, audio réel, dérive, reprise réseau, installation et mise à
  jour restent à qualifier. Les 38 échecs React existants restent ouverts.
- Aucun service de production n'a été lancé ou arrêté ; aucune release publiée.
