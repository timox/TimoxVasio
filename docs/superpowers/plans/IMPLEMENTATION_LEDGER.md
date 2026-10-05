# État d’implémentation TimoxVasio

> Mis à jour le 5 octobre 2026. Ce ledger remplace le suivi du prototype à quatre pilotes. Les relevés datés du 4 octobre et antérieurs sont historiques; le statut final est consigné ci-dessous.

## Contrat courant

Le produit cible utilise une DLL ASIO unique, `TimoxVasio.dll`, annoncée sous le nom `TimoxVasio`, avec une capacité maximale de 256 entrées et 256 sorties. `TimoxVirtualAsioEngine.exe` est le processus moteur distinct. Le périphérique ASIO physique sélectionné fournit la fréquence et la taille de buffer effectives. L’API est le contrat d’inventaire et de configuration consommé par l’interface.

La conception et les critères d’acceptation sont définis dans [la spécification 256 canaux](../specs/2026-10-03-vasio-single-driver-256-channel-design.md). Le suivi des tâches se trouve dans [le plan maître](2026-10-03-vasio-256-physical-master-plan.md).

## Validation finale — 5 octobre 2026

- Build/release : `v1.0.0` publiée sur [timox/TimoxVasio](https://github.com/timox/TimoxVasio/releases/tag/v1.0.0). Le Setup Electron, la DLL ASIO et le moteur ont été vérifiés contre leurs SHA-256 de release.
- Réinstallation locale : Timox VASIO Control 1.0.0 installé par le Setup; `TimoxVasio.dll` réinstallée et enregistrée sous `HKLM\SOFTWARE\ASIO\TimoxVasio`; le moteur lancé depuis `resources/backend/TimoxVirtualAsioEngine.exe` dans l’installation Electron.
- Son : l’utilisateur confirme le test audible de bout en bout avec Renoise et SSL ASIO Driver 1. L’API indique `running`, 48 kHz/1024 frames, un client Renoise avec 64 entrées/sorties et huit routes. Le flux `audio.meter` a livré 60 événements en 3,5 secondes, crêtes de `-101,65` à `-26,43 dBFS`.
- Journaux : `/api/v1/diagnostics` et `%LOCALAPPDATA%/TimoxVasio/logs/engine.log` contiennent le démarrage, la configuration physique et l’application des huit routes.
- Swagger : vérifié par l’utilisateur dans l’application Electron installée.

## Éléments réalisés et vérifications disponibles

- Le transport partagé versionné et le pilote unique prennent en charge 256 canaux par direction et publient les canaux alloués par client.
- La configuration fréquence/taille transmise aux clients utilise maintenant une seule paire de valeurs globale, cohérente avec l’unique pilote virtuel. Compilation x64 de `TimoxVirtualAsioEngine` et `VasioClientManagerProbe` réussie après initialisation de `VsDevCmd`.
- Le moteur découvre les clients TimoxVasio, pilote l’hôte ASIO physique et relie les routes par le graphe audio.
- La configuration passe par l’API HTTP/WebSocket documentée; l’interface Electron consomme ce contrat.
- Les guides actifs, le nom de l’exécutable et les ressources de packaging utilisent les noms Timox.
- Les vérifications logicielles rapportées incluent les probes transport, graph, runtime, API et GUI, la compilation x64, ainsi que l’énumération COM enregistrée de `TimoxVasio` à 256/256.
- Le script d’installation migre les entrées de registre VASIO connues vers `TimoxVasio`. État confirmé le 3 octobre : HKLM ne contient que `TimoxVasio` parmi les noms du produit, les anciens CLSID sont absents et le dossier d’installation ne contient plus que `TimoxVasio.dll`.
- État observé le 3 octobre : `DriverProbe --registered TimoxVasio` annonce 256 entrées et 256 sorties; `VasioClientManagerProbe` confirme l’attachement interprocessus et la propagation de 96 kHz/512 frames; `VasioExternalHostProbe` découvre le pilote via `AsioDriverList`, puis valide `init/createBuffers/start`, reset/restart et reopen à 44,1 kHz/256 frames avec le moteur courant. `--list-asio` énumère les pilotes physiques, dont les entrées SSL, sans ouvrir de périphérique. Ces probes ne valident pas un flux audio matériel réel; les paramètres 44,1 kHz/256 frames du probe sont les valeurs par défaut avant sélection d’un maître physique.
- Session de reprise le 3 octobre : compilation x64 de `TimoxVirtualAsioEngine` réussie après le correctif d’initialisation des buffers physiques. Par l’API documentée, configuration de `SSL ASIO Driver 1` sans route confirmée à l’état `stopped`, 48 kHz/1024 frames; l’API publie ses capacités réelles (16 entrées, 8 sorties). La page de contrôle répond sur le port 4000. Aucun client TimoxVasio n’était connecté pendant cette vérification, donc elle ne prouve pas encore le transfert matériel d’un signal.

## Couverture complémentaire

- Vérifier avec un hôte ASIO réel la découverte, l’allocation clairsemée et le rejet des paramètres incompatibles.
- Construire depuis l’interface un circuit avec le périphérique physique réel, puis mesurer les trois flux audio de la spécification, dont un canal matériel supérieur à 6.
- Vérifier les scénarios de reconfiguration, déconnexion/reconnexion et erreur avec le matériel ciblé.

Le test de bout en bout sur ce poste est réalisé. Une compilation, une sonde COM ou une énumération ne remplace pas les vérifications complémentaires d’autres hôtes et transitions listées ci-dessus.

## Reprise du 4 octobre 2026

- Le registre x64 et le probe PortAudio compilé dans ce dépôt trouvent
  `TimoxVasio` à 256 entrées et 256 sorties. Le probe accepte 48 kHz lorsque
  l’horloge physique est réglée à 48 kHz.
- La capture Mixxx reste cohérente avec un filtre interne, pas avec une absence
  d’enregistrement ASIO. Le journal de Mixxx identifie la build
  `2.6-beta-402-ge1c1e5b72b`. À ce commit, `SoundDevicePortAudio` convertit les
  comptes de canaux en `ChannelCount`, dont `value_t` est `uint8_t`; 256 devient
  invalide, puis `SoundManager::getDeviceList()` écarte le périphérique quand
  ses deux directions sont invalides. Le diagnostic détaillé se trouve dans
  [la note de compatibilité Mixxx](../mixxx-256-channel-compatibility.md).
- Un correctif minimal pour élargir `ChannelCount::value_t` est conservé dans
  `patches/mixxx/0001-audio-channel-count-support-256.patch`. Il est appliqué
  à une copie de source Mixxx 2.7 sous `vendor/mixxx-2.7-256`; la compilation
  x64 a produit `build_mixxx_2.7_256/mixxx.exe`. Le 4 octobre, l’utilisateur
  a lancé cette copie et confirmé qu’elle découvre TimoxVasio. Cela ne prouve
  pas encore l’ouverture du flux ni l’allocation des 256 canaux. Ce n’est pas
  la version installée : Mixxx 2.6 beta reste inchangé et non corrigé.
- L’API moteur a été remise à `SSL ASIO Driver 1`, 48 kHz/512 frames, état
  `stopped`, sans route et sans client virtuel. Ce réglage confirme l’état de
  configuration, pas un transfert audio. Le test direct de la DLL PortAudio
  fournie avec Mixxx reste bloqué dans `Pa_Initialize()` en sonde isolée; il
  n’est pas utilisé comme preuve du diagnostic ChannelCount.

## Reprise après interruption — noms physiques et état API

- `PhysicalAsioHost` interroge chaque canal du pilote sélectionné avec
  `IASIO::getChannelInfo`; le serveur API publie le nom fourni par le pilote
  dans `endpoint.name`, avec un nom de remplacement seulement si le champ du
  pilote est vide. Le chemin est commun à tous les pilotes physiques, sans
  branche spécifique SSL.
- Interrogation effective de `SSL ASIO Driver 1` à 48 kHz/1024 frames, sans
  route et donc sans démarrage de flux. L’API a retourné 16 entrées nommées
  (`Analogue 1–4`, `Talkback`, `Loopback L/R`, `ADAT 1–8`) et 8 sorties
  (`Mon L/R`, puis `Out 3–8`). Après lecture, la configuration a été libérée.
- L’API documentée répond ensuite encore avec `state: stopped`, aucun pilote
  sélectionné et sans erreur. L’inventaire virtuel expose Renoise PID 19488,
  avec 64 entrées et 64 sorties actives. Ce constat ne prouve pas le transfert
  de signal vers la SSL.
- Le journal Electron précédent enregistre la sortie du moteur enfant avec
  `signal=SIGTERM`; le code Electron envoie ce signal dans `before-quit`. Ce
  journal explique la disparition de l’API lorsque le processus Electron
  quitte, mais ne permet pas d’attribuer cette fermeture à la commande Apply.
- Le message de succès de la GUI précise maintenant que l’état `stopped` est
  attendu lorsque la configuration appliquée ne comporte aucune route.
- `npm run react-build` compile avec succès. La création du paquet Electron
  échoue dans `electron-builder` lors de la création de son cache dans
  `AppData\Local`; aucun paquet Electron actualisé n’a été produit.
- Un essai de démarrage avec une route explicitement muette de Renoise vers
  `SSL ASIO Driver 1 · Mon L` a d’abord été confirmé par l’API à `running`,
  48 kHz/1024 frames. Peu après, une fenêtre Windows a signalé une écriture
  mémoire invalide dans `TimoxVirtualAsioEngine.exe` et le processus/API ont
  disparu. Le journal Windows ne fournit pas de rapport correspondant; la
  cause précise reste inconnue.
- Une option diagnostique CMake, désactivée par défaut,
  `VASIO_ENABLE_CRASH_DUMP`, écrit un minidump près de l’exécutable en cas
  d’exception non gérée. La build diagnostique x64 a été compilée puis
  démarrée sous PID 23388. Renoise a republié ses 64 entrées/sorties. Une
  route muette de Renoise Out 1 vers SSL `Mon L` a été acceptée par l’API,
  puis le moteur s’est arrêté sur une violation d’accès (`0xc0000005`,
  adresse `0x567250`). Un minidump a été produit.
- Le crash a été reproduit avec la même route après reconstruction en
  `RelWithDebInfo`; l’API a accepté la configuration avant le nouvel arrêt
  (`0xc0000005`, adresse `0x560FA0`). Le minidump correspondant et
  `TimoxVirtualAsioEngine.pdb` ont confirmé un saut indirect vers un pointeur
  périmé. La source Steinberg incluse dans `asiosdk/driver/asiosample`
  conserve le `ASIOCallbacks*` passé à `createBuffers`; l’exemple d’hôte garde
  sa structure globale. Notre hôte créait cette structure comme variable
  locale dans `PhysicalAsioHost::start`, dont la pile est réutilisée après le
  retour de la méthode. Elle est maintenant un membre de `Session`, et le
  pilote reçoit `&session.callbacks`.
- Après recompilation et redémarrage, la route muette a tenu en état `running`
  sans erreur; la même route activée (Renoise Out 1 vers SSL `Mon L`, 48 kHz,
  1024 frames) est restée `running` pendant huit secondes, avec une route et
  aucun nouveau dump. L’API a accepté et confirmé la configuration. Le flux
  audio audible n’a pas été mesuré indépendamment.

## Archive

Le prototype historique `CMakeLists_PortAudio.txt` et `src/main_portaudio.cpp` créent encore VASIO1–VASIO4 à six canaux. Le fichier orphelin `src/vasio_driver_correct.cpp` contient aussi un ancien pilote six canaux. Aucun n’est référencé par le build principal. La cible du prototype PortAudio est nommée `LegacyVASIO_PortAudioPrototype` pour éviter de la confondre avec `TimoxVirtualAsioEngine`. `config/routing.ini` est conservé uniquement pour ce prototype et n’est pas la configuration active du moteur.

## Reprise avant redémarrage utilisateur — 4 octobre 2026

### Objectif

Fournir sous Windows x64 un pilote ASIO virtuel unique, `TimoxVasio`, jusqu’à
256 entrées et 256 sorties, avec une interface graphique reliant les canaux
actifs des applications aux ports d’un pilote ASIO physique. Le pilote
physique maître fixe la fréquence et la taille de buffer du moteur et du
pilote virtuel. L’interface applique la configuration uniquement via l’API
documentée.

### État à reprendre

- L’API du moteur actif a été relevée avec SSL ASIO Driver 1, 48 kHz,
  1024 frames et quatre routes stéréo Renoise/Ableton vers Monitor L/R. Renoise
  et Ableton sont publiés comme clients; Mixxx reste à 0 entrée/sortie.
- Le build `build_engine_names_check` contient les libellés SSL 12 résolus,
  mais le moteur actif `build_engine_vs2026_ninja` n’a pas été remplacé. Les
  noms génériques `Out 3` à `Out 8` restent donc attendus jusqu’à la prochaine
  installation/redémarrage contrôlé.
- À la date de cette ancienne note, le signal audio de bout en bout n’était pas
  confirmé; le relevé `-120 dBFS` était sans contexte de lecture. Le test
  ultérieur et ses mesures sont consignés dans « Validation finale — 5 octobre
  2026 » en tête de ce document.
- Mixxx installé reste 2.6 beta x64; sa limite `ChannelCount` 8 bits écarte
  TimoxVasio à 256/256. Le patch est essayé sur la copie source 2.7 uniquement.
- L’interface de matrice et la documentation ont été mises à jour. `gui/INTEGRATION.md`
  décrit maintenant le pilote virtuel unique; le guide GUI et la note Mixxx
  expliquent la limite observée. L’`ASIO_AUDIT.md` garde les faits de session
  et distingue les validations logicielles du signal matériel.
- L’utilisateur annonce qu’il va redémarrer. Ne pas redémarrer, tuer ou
  piloter des hôtes audio et ne pas valider le runtime tant qu’il n’a pas
  indiqué que le redémarrage est terminé.

### Reprise — liste historique, test de bout en bout depuis réalisé

Les actions de cette liste décrivaient l’état antérieur au retour de test de
l’utilisateur. Le parcours audio de bout en bout a depuis été testé et confirmé
le 5 octobre 2026; elles ne constituent plus un blocage de validation audio.

1. Reprendre la build Mixxx en cours si elle a été interrompue; elle ne touche
   pas l’installation. Même si elle réussit, traiter Mixxx installé comme
   non corrigé jusqu’à installation explicite d’une version correspondante.
2. Après le retour de l’utilisateur et fermeture/redémarrage contrôlé,
   recompiler/installer le moteur courant, puis vérifier via l’API les noms de
   ports physiques, le statut et la capacité du pilote sélectionné.
3. Pendant la lecture d’un signal, vérifier les mètres, les compteurs et le
   son physique, sans supposer qu’une route confirmée signifie un son audible.
4. Vérifier la découverte et l’ouverture du pilote avec un hôte ASIO réel;
   résoudre l’incompatibilité Mixxx séparément. Ces actions sont conservées
   comme pistes de couverture supplémentaire, et non comme condition du test
   de bout en bout déjà réalisé.

## Audit du livrable — 4 octobre 2026

### Interface, accès moteur et diagnostics

- L’interface Electron inclut les vues Configuration, API/Swagger et Journaux.
  Swagger UI est embarqué localement dans l’application, sans dépendance à un
  CDN. Les réglages visuels augmentent la taille de texte, le contraste et la
  visibilité des surfaces et du focus clavier.
- L’interface sait démarrer et arrêter le moteur par le contrat API/WebSocket;
  la fermeture de la fenêtre laisse le moteur actif. L’arrêt est refusé tant
  qu’un client virtuel est attaché. Le point d’accès moteur reste
  `TimoxVirtualAsioEngine.exe`, distinct de la DLL ASIO.
- Le moteur écrit des journaux JSONL avec niveaux configurables, lecture
  bornée par l’API et rotation limitée. Les événements de configuration et de
  démarrage sont journalisés.
- Vérifications logicielles de cette reprise : tests natifs
  `EngineDiagnosticsTests` et `ApplicationProfilesApiTests`, tests React,
  tests client API et contrats, script PowerShell de contrat avec pwsh 7,
  build React et création des deux paquets Electron candidats ont réussi.
  L’archive a été inspectée pour confirmer Swagger embarqué et le moteur
  empaqueté correspond au binaire compilé. Ces vérifications ne valident pas
  le rendu visuel interactif ni le son physique.
- Paquets candidats précédents présents dans `gui/dist-candidate` :
  `VASIO Control 1.0.0.exe` et `VASIO Control Setup 1.0.0.exe`. Ils n’ont pas
  été lancés pendant cet audit, car une ancienne interface et un ancien moteur
  étaient déjà actifs.

### Dernier état runtime observé en lecture seule

- Le 4 octobre, le moteur actif était l’ancien binaire
  `build_engine_vs2026_ninja/TimoxVirtualAsioEngine.exe` (PID 20452), avec
  l’ancienne interface Electron (PID 9128) et Mixxx (PID 19832). Le port API
  52525 appartenait à ce moteur. Aucun processus n’a été arrêté, remplacé ou
  reconfiguré pendant l’audit.
- L’API de cet ancien moteur déclarait `running` à 48 kHz. Cette réponse
  tronquée ne permet pas d’attester les routes, la taille de buffer ou le
  niveau du signal. Elle ne doit pas être confondue avec une vérification du
  nouveau candidat ni comme preuve d’un son audible.
- À la date de ce relevé, le signal matériel de bout en bout, les trois flux
  audio d’acceptation, les cas de reconfiguration/déconnexion et le comportement
  avec l’installation Mixxx ciblée restaient à valider. Cette photographie
  historique précède le test de bout en bout réalisé et confirmé par l’utilisateur
  le 5 octobre 2026. L’utilisateur avait annoncé la
  fin de son redémarrage/test; toute validation runtime ultérieure attend son
  action de lancement/essai et son retour.

### Publication — état du 4 octobre, remplacé le 5 octobre

- La notice de licence explique pourquoi le code reste en GPL-3.0-only avec
  la voie libre du SDK ASIO et précise que le nom officiel reste
  `TimoxVasio`. La licence libre autorise les forks renommés; elle ne peut
  donc pas garantir à elle seule le nom des versions dérivées.
- Le README pointe maintenant vers le dépôt public nommé `TimoxVasio`, selon
  l’indication de l’utilisateur. L’accès au clone/remote de cette cible n’est
  pas vérifiable depuis le dépôt `asio` ouvert ici.
- À cette date, aucun commit, tag ou push de publication n’avait été créé. Le dépôt ouvert ici
  est `grrzzzz` et son remote pointe vers `timox/grrzzzz`, tandis que la cible
  publique demandée est le dépôt séparé `TimoxVasio`. Ne pas publier dans le
  dépôt `grrzzzz` par substitution. La migration/publication attend le clone
  correct de `TimoxVasio` dans le périmètre de travail autorisé.

Cet état du 4 octobre a été remplacé : la release `v1.0.0` a depuis été publiée
sur `timox/TimoxVasio` et le test audio de bout en bout confirmé le 5 octobre.

### Matrice de preuves du relevé du 4 octobre 2026 (historique)

| Critère | État établi | Preuve et limite |
|---|---|---|
| DLL x64 unique, identité `TimoxVasio`, 256/256; installation sans les anciennes entrées du produit | Vérifié par les probes et le contrôle registre rapportés plus haut | Énumération/COM et registre local; à recontrôler dans l’installation de publication |
| Profils par application, limites Mixxx et allocation de canaux bas, élevés et clairsemés | Vérifié par tests/probes logiciels; découverte Mixxx réelle confirmée | L’allocation effective dans Mixxx stable garde sa limite documentée; le test audio de bout en bout est confirmé séparément |
| Transport partagé multi-client et identité des endpoints | Vérifié par probes interprocessus et inventaires consignés | Ne remplace pas le test simultané de plusieurs hôtes réels dans le circuit final |
| Trois familles de routes dans le graphe, avec canaux physiques au-delà de 6 | Vérifié dans les tests simulés du runtime/graphe | Le test du canal 10 utilise des buffers simulés; aucun signal SSL mesuré |
| Négociation du taux, relecture des capacités physiques et valeurs ASIO effectives | Vérifié par probes et interrogation de SSL ASIO Driver 1 consignées | Ces sondes documentent le protocole; le transfert de bout en bout a été testé séparément et confirmé par l’utilisateur |
| API documentée, interface, profils, Swagger embarqué, journaux et contrôle du moteur | **Vérifié sur la version 1.0.0 installée** | Swagger confirmé par l’utilisateur; diagnostics API et `engine.log` consignent le démarrage et la configuration |
| Parcours audio de bout en bout avec routage | **Testé; confirmé par l’utilisateur le 5 octobre 2026** | Renoise, 64 entrées/sorties, huit routes, SSL ASIO Driver 1 à 48 kHz/1024 frames; 60 événements `audio.meter` sur 3,5 s, crêtes de -101,65 à -26,43 dBFS |
| Reconfiguration, redémarrage, déconnexion/reconnexion, refus d’incompatibilité et panne observable | **Partiellement vérifié** | Tests et probes couvrent des branches logicielles; validation complète sur l’hôte et le périphérique ciblés manque |
| Licence GPL fournie avec l’application et avis juridique accessible | **Ajoutée au paquet candidat; accès UI non prévu** | Le paquet Electron est configuré pour embarquer `LICENSE`, `LICENSING.md` et l’avis du SDK ASIO dans `resources/legal/`. Aucun écran Licence n’est implémenté; le titulaire des droits n’est pas identifié dans la notice et ne doit pas être inventé |
| Publication versionnée dans le dépôt public `TimoxVasio` | **Réalisée : `v1.0.0`** | Release et binaires publiés; voir les notes de version |

Cette matrice historique a été actualisée avec le résultat du test et de la
publication. Les réserves du 4 octobre sur l’audio et l’absence de release sont
levées. La couverture d’autres hôtes et transitions ainsi que les contrôles
d’accessibilité restent des extensions de périmètre, non des blocages de la
version 1.0.0.

### Reprise UX/UI, moteur et journaux

- La revue UX/UI fondée sur cinq captures utilisateur est consignée dans
  [`UX_UI_REVIEW.md`](../UX_UI_REVIEW.md), avec les captures conservées sous
  `docs/ux-audit/evidence/`. Elles montrent la configuration et les routes,
  mais pas les vues Journaux/Swagger ni la build candidate après corrections.
- Dans les captures, l’en-tête affiche « VASIO Control » et aucune navigation
  vers Journaux/Swagger n’est visible; la build candidate actuelle expose ces
  vues et porte maintenant le titre « Timox VASIO Control ».
- Les anciennes règles CSS qui rétablissaient des titres cyan à 18 px et des
  barres de défilement cyan lumineuses ont été corrigées. Le build React et les
  paquets Electron candidats ont été reconstruits après ce changement.
- La légende de matrice affiche maintenant le nombre de routes sur la page
  courante, ou précise qu’aucune route ne figure dans cette vue. Le build React
  et les paquets Electron candidats ont été reconstruits après cette
  correction observée sur les captures.
- Le moteur de développement est
  `build_engine_profile_audit_bin/TimoxVirtualAsioEngine.exe`. La copie
  empaquetée se trouve dans
  `gui/dist-candidate/win-unpacked/resources/backend/TimoxVirtualAsioEngine.exe`.
  Leur SHA-256 est identique :
  `2D97CE37F53B012E685498C81CF61CDF089873261412C36AACBBC454BF9380D5`.
- L’interface expose le réglage `info/debug`, la lecture des diagnostics et le
  démarrage/arrêt contrôlé du moteur dans l’onglet « Journaux ». Le chemin
  configuré est `%LOCALAPPDATA%/TimoxVasio/logs/engine.log`; ce fichier et son
  dossier n’existaient pas dans le profil vérifié à cette reprise. La création
  effective du journal doit être constatée après lancement du nouveau moteur.
- Le nom public de l’application Electron est **Timox VASIO Control**. Les
  paquets reconstruits portent ce nom; l’identifiant interne reste stable
  (`com.vasio.control`) afin de conserver l’identité d’installation.
- Les notes de version candidates sont dans
  [`RELEASE_NOTES_v1.0.0.md`](../../RELEASE_NOTES_v1.0.0.md); elles signalent
  l’absence de preuve audio matérielle et les limites de l’audit visuel.
