# PWAR — état de reprise

Mis à jour le 25 octobre 2026 à 23 h 52, heure locale.

**Travail suspendu à la demande de l'utilisateur : priorité au debug en cours.**
Ne pas relancer les essais PWAR, modifier le routage ou redémarrer le moteur
pendant ce debug. L'objectif n'est pas terminé.

## Objectif conservé

Intégrer le client PWAR à TimoxVasio pour envoyer l'audio Windows vers PipeWire
Linux et récupérer le signal traité, tout en conservant la carte ASIO locale.
Configuration/supervision par GUI et API, contrôle distant authentifié,
continuité, adaptation des horloges et reprise réseau restent dans l'objectif.

## Disponible

- Client ASIO autonome : `build_pwar_reconciled_tests/Release/PwarAsioClient.exe`.
  Il utilise le pilote virtuel Timox sans remplacer la carte physique.
- SHA-256 vérifié à cette reprise :
  `9E47FDE737D54552E0882D8FFF2D49AB912BDB92CE3247A39341634720CA8441`.
- Contrôle JSON stdin/stdout implémenté : démarrage `--stdio`, commandes
  `status` et `stop`, événements et arrêt sur fermeture stdin.
  Contrat : [CONTROL.md](patches/pwar/client/CONTROL.md).
- Limites actuelles : stéréo float32, 48 kHz, ports UDP 8321 fixes,
  bloc ASIO préféré jusqu'à 4096 frames. Latence et dérive non mesurées : `null`.
- Backend Linux à deux streams PipeWire, files audio et correctifs de protocole.
  Sources consolidées : `build_pwar_reconciled_20261025`.
  Correctifs et tests conservés dans `patches/pwar`.
- Correctif 0015 d'aide CLI : patch vérifié, syntaxe compilée sous Linux avec
  avertissements traités en erreurs ; pas déployé sur le relais actif.

Les anciens builds PWAR ont été nettoyés. Les deux dossiers ci-dessus sont
conservés pour la reprise et l'intégration. Aucune PR upstream publiée.

## Ce qui n'est pas démontré

Les essais de transport et de supervision ne prouvent pas le trajet complet.
Les précédentes observations duplex échangeaient notamment du silence dans
le sens Windows vers Linux. Elles ont aussi montré des pertes/silences : ne
pas présenter le client comme stabilisé.

L'aller-retour via une vraie source Windows, le traitement DSP Linux, la
continuité prolongée, la compensation de dérive, la reprise après déconnexion
et l'ensemble GUI/API authentifiée restent à réaliser ou à valider.
Piste de réutilisation PipeWire pour la dérive, non implémentée :
[PIPEWIRE_CLOCK_ADAPTATION.md](patches/pwar/PIPEWIRE_CLOCK_ADAPTATION.md).

## Coordination avec Timox

Canal unique : [REPLY_TIMOX.md](patches/pwar/REPLY_TIMOX.md).
Messages numérotés `PWAR-*` / `TIMOX-*`, accusés explicites. La messagerie
inter-agents ne traverse pas les deux sessions ; ne pas compter sur
`/root/pwar_status` pour joindre l'autre session.

L'agent PWAR ne modifie pas les autres composants Timox. L'agent Timox prend
en charge son superviseur, l'API et TVSD sans modifier le code PWAR.
Il a confirmé que le contrat suffit pour commencer. Dernier état communiqué :
superviseur commencé dans `include/pwar_client_supervisor.h` et
`src/pwar_client_supervisor.cpp`, raccordement complet non confirmé.
Retour de revue PWAR-005 : vérifier `version == 1` avant d'accepter un message.

Le refus de sauvegarde des routes venait probablement du contexte de droits
du moteur. Timox a annoncé sa correction et un `configuration.apply` réussi,
révision 2 vers 3, 38 routes conservées. Ne pas réappliquer de modifications
de permissions ni redémarrer le moteur sans réexaminer le contexte courant.

## État des ressources à la suspension

- Aucun processus `PwarAsioClient` Windows observé lors de la rédaction.
- Dernier relais Linux lancé : PID **3467277**, sur `10.1.0.101`, pair Windows
  `10.1.0.104`, UDP 8321. **Son arrêt n'a pas été demandé depuis ce lancement ;
  considérer qu'il peut encore tourner et vérifier avant toute action.**
- Derniers liens vérifiés : `pwar-receive:output_FL` vers `pwar-send:input_FL`,
  et FR vers FR ; aucun lien de ce relais vers les sorties physiques.
- Journal Linux :
  `/tmp/timox-pwar-qualification.HYVsLU/coordinated-roundtrip.log`.
- Accès SSH existant : alias `timox-linux`, configuration locale
  `C:/Users/timo/.ssh/timoxvasio_config`. Pas de mot de passe ni de clé dans ce document.
- PWAR-004 avait donné la main à Timox pour une observation Windows unique.
  Aucun résultat reçu à la rédaction ; PWAR-006 demande résultat ou restitution
  explicite de la main. La suspension utilisateur remplace cette invitation :
  ne pas démarrer l'essai maintenant.

## Reprise, quand l'utilisateur la demande

1. Lire les derniers échanges dans REPLY_TIMOX.md et identifier l'agent qui
   prend la main sur l'essai. Vérifier les processus et ports réellement actifs.
2. Vérifier l'état actuel du moteur et des routes, puis la disponibilité du
   relais ; ne pas déduire cet état des anciens journaux.
3. Exécuter une seule observation coordonnée avec
   `node patches/pwar/client/observe-roundtrip.cjs` si le contexte le permet.
   Le script utilise la sonde ASIO existante et deux routes temporaires ; il
   conserve les requêtes/réponses API et `after-cleanup.json` sous le build actuel.
   Il retire uniquement ses routes avec contrôle de révision.
4. Examiner le signal retourné et le nettoyage. Corriger les défauts concrets,
   puis ajouter le traitement Linux et les validations restantes de l'objectif.

Ne pas reprendre une campagne de tests isolés sans lien avec ce trajet.
