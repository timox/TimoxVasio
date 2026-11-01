# Ponts de processus : noms, catalogue et sorties d'origine

**Statut : travail en cours lié aux préversions non publiées.** Ce document ne
décrit pas une release correspondante ; aucune release, aucun tag et aucun
livrable stable ne doit être créé à partir de ce lot.

## Comportement retenu

- Nom humain éditable généré à la création : `ProcessCopy+firefox.exe`.
  L'identifiant technique du pont reste distinct et stable pour les routes.
- Sauvegarde depuis la modale du pont vers le catalogue moteur. Un modèle
  conserve `id`, `name`, `processName` et `bufferDuration100ns`, jamais un PID,
  une date de création de processus ou un chemin d'exécutable.
- Réapplication : inventaire Windows frais, correspondance sur le nom de
  l'exécutable, choix explicite si plusieurs instances. Une absence laisse le
  modèle disponible sans inventer une instance ni démarrer un processus.
- Sorties Windows d'origine affichées lors de la création/réapplication.
  Elles viennent de l'inventaire et de l'identité exacte de l'instance ; une
  sortie inconnue n'est pas remplacée par une ancienne observation.
  Elles ne sont pas figées dans le modèle réutilisable.
- Le pont copie le son ; il ne redirige ni ne coupe la sortie Windows originale.
- Un pont de processus indisponible n'est plus proposé pour une nouvelle
  connexion. Sa configuration et ses routes sauvegardées restent présentes.
  Un pont actif silencieux reste connectable.

## Répartition

**TimoxVAudioEngine** : `GET/PUT /api/v1/process-copy-templates`, persistance
atomique du catalogue, validation stricte, propriété optionnelle `name` des
ponts et capability `processCopyTemplates`. Contrat OpenAPI/JSON Schema et docs
FR/EN mis à jour. L'activation conserve la validation PID/date/chemin ; aucun
affaiblissement de la protection contre la réutilisation d'un PID.

**TimoxVSSC** : commandes Electron, nom par défaut, actions de catalogue dans
les modales, choix de l'instance courante, sorties d'origine et filtrage des
sources indisponibles dans l'ajout de connexion. Sur un ancien moteur, les
fonctions de catalogue ne sont pas annoncées comme disponibles.

**TimoxVasio / Timox-Pwar** : aucun changement de pilote ni de transport.
Les copies anciennes moteur/GUI de TimoxVasio ne sont pas les sources canoniques.

## Validation

- 48 tests React ciblés ; tests Node/Electron/Swagger/contrats et build GUI CI.
- Tests C++ API profils/catalogue, API ponts, persistance de configuration.
- Test de non-réutilisation d'un processus terminé : comportement existant
  déjà conforme ; aucun correctif de production du manager nécessaire.
- Tests conversion PCM et rééchantillonnage isolés ; ils ne reproduisent pas
  le hachage Firefox réel. Voir le diagnostic Firefox/SSL séparé.
- Réponse HTTP et événement WebSocket réels des tests validés par JSON Schema.

## Limites

La découverte actuelle expose les sessions audio Windows actives, pas tous les
processus vivants. Une application peut donc devoir produire du son pour être
sélectionnée. Le catalogue utilise le remplacement complet, comme les profils
existants : deux contrôleurs concurrents peuvent écraser leurs modifications.

Le signal sonore réel n'est pas qualifié par ce lot. Aucun redémarrage ni
changement de configuration audio de la session utilisateur n'a été réalisé.

## Transfert de travail vérifié localement — non publié

52 fichiers sources/artefacts ont été transférés localement vers les dépôts
canoniques pour travail de préversion, avec
sauvegarde et vérification SHA-256 (`build_processcopy_20261027/transfer-result.json`).
Le code source du lot n'est pas intégré ici comme fonctionnalité publiée ; ce
document et ses artefacts restent uniquement le suivi du travail de préversion.

- Moteur : `../TimoxVAudioEngine/build/Release/TimoxVirtualAsioEngine.exe`,
  1 527 296 octets, SHA-256
  `4a5b8efc404648be905a7a12b899f449fa5f86598477f46211e5ff8250eb7c9c`.
- Portable local non publié : `../TimoxVSSC/dist/TimoxVSSC-0.1.0-processcopy.exe`,
  71 577 054 octets, SHA-256
  `7bbbd22c80eec629620c533327c48ff2e2e7551699afa678a2bf2245b5e5cfa3`.
- Moteur, PWAR, OpenAPI et schéma extraits du portable comparés aux entrées
  épinglées. `verify-import.cjs` valide 163 entrées et 5 pins ABI ;
  `verify-artifacts.cjs --packaged` passe dans le dépôt GUI canonique.
- Compilation Release moteur, 48 tests React, 35 Node/Electron/Swagger/contrats,
  et les six cibles C++ API profils/catalogue, API ponts, persistance,
  contrôleur WASAPI, conversion PCM et rééchantillonnage : réussis.

Le portable n'a pas été lancé ; il ne remplace pas le moteur déjà actif et ne
constitue aucune release.
