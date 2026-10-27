# Audio natif Windows : pilote, transport et contrat PCM

Ce dossier regroupe le contrat PCM, le prototype de pilote PortCls/WaveRT et les composants du broker du [lot pilote natif](../docs/superpowers/specs/2026-10-22-native-windows-audio-driver.md). Le pilote natif reste distinct de `TimoxVasio.dll`, qui conserve son fonctionnement ASIO autonome. Les endpoints natifs ne sont pas encore disponibles dans l'installation utilisateur : compilation et tests de composants ne prouvent ni le chargement du pilote, ni un échange audio réel.

## État des composants

| Composant | Sources et procédure | Limite actuelle |
|---|---|---|
| Pilote noyau et contrôle IOCTL | [driver/README.md](driver/README.md), [BUILDING.md](BUILDING.md) | Prototype compilé, catalogue non signé ; chargement et échanges WaveRT à qualifier |
| Paquets, géométrie, sessions et files bornées | `include/`, `tests/`, CMake autonome ci-dessous | Tests en espace utilisateur, sans exécution noyau |
| Transport IOCTL Windows, réservé au futur broker | Cible `NativeAudioWindowsDriverTransport`, `../src/native_audio_transport_win32.cpp` | Ouvre uniquement le device privé ; à construire dans le processus du service, jamais dans l'engine ou la DLL ASIO |
| Adaptateur direct de laboratoire | `CreateDirectNativeAudioLabTransport` dans `../src/native_audio_transport_win32.cpp` | Injection OS pour qualification isolée ; ne constitue pas le canal utilisateur de production |
| Broker, sessions, AF_UNIX et Schannel | `broker/`, tests `../tests/native_audio_*` | Le peer PID, son jeton et le compte/session console WTS sont comparés avant admission ; transfert de handle et isolation audio restent à qualifier |
| Probe du compte de service et de la publication | [broker/SERVICE-PROBE.md](broker/SERVICE-PROBE.md) | Outil de qualification distinct du broker audio ; exécution SCM pas encore qualifiée |
| Raccord au graphe, contrôleur et API | `../src/native_audio_backend.cpp`, `../src/native_audio_manager.cpp`, `../src/audio_controller.cpp` | Activation publique désactivée ; ne pas confondre les ponts WASAPI existants avec ces endpoints virtuels |

Le moteur possède maintenant un fournisseur de transport socket de production : à chaque tentative d'ouverture native, il relit la découverte protégée sous `ProgramData\TimoxNativeAudioBroker\Runtime`, vérifie le descripteur et l'ACL, puis épingle le serveur avec le SHA-256 publié. Il lit l'empreinte SHA-256 du certificat client dans `HKLM\SOFTWARE\Timox\NativeAudioBroker\Runtime\ClientCertificateSha256` (valeur `REG_BINARY`, 32 octets) et exige un certificat correspondant dans `CurrentUser\My`, valide, avec EKU Client Authentication et clé privée accessible. La connexion AF_UNIX utilise le chemin UTF-8 canonique du répertoire validé ; le chemin trop long ou non découvrable échoue fermé. L'engine injecte ce fournisseur au contrôleur ; l'absence de provisioning est signalée comme indisponibilité, et un certificat/enrôlement invalide comme refus d'accès.

Le contrat de provisioning décrit la sélection du certificat, mais le service, l'installateur qui écrit cette valeur, l'enrôlement effectif du certificat et le pilote chargé ne sont pas encore raccordés. Le build moteur ne prouve donc pas une connexion broker réelle ni un flux audio. La suite et les conditions d'activation sont décrites dans le [plan de production](../docs/superpowers/plans/2026-10-22-native-audio-production.md). Il reste notamment à établir l'isolation audio entre logons, l'authentification de l'API et de ses clients, le service déployé, l'installation/retrait du pilote et les performances audio réelles. La GUI affiche actuellement l'état natif, sans rendre ces conditions facultatives.

## Validation des paquets

`include/native_audio_protocol.h` fournit le vocabulaire compatible C ; `include/native_audio_packet_validation.h` fournit le validateur C++17 sans dépendance Windows. Celui-ci décode des octets potentiellement non alignés, vérifie les bornes et renvoie les métadonnées acceptées, ou une erreur avec métadonnées nulles. Il n'alloue pas, ne verrouille pas, ne fait aucune syscall et ne parcourt pas le payload.

## Wire v1.0

Tous les entiers sont little-endian ; aucune structure C/C++ n'est directement sérialisée. Le paquet possède exactement 64 octets d'en-tête puis le PCM interleavé signé 32 bits, deux canaux à 48 kHz. C'est le format initial explicitement supporté ; tout autre format est refusé.

| Offset octet | Taille | Champ |
|---|---|---|
| 0 | 4 | ASCII `TVWA` |
| 4 | 4 | Taille en-tête : 64 |
| 8 / 10 | 2 / 2 | Version majeure 1 / mineure 0 |
| 12 | 2 | Opcode : 1 paquet rendu, 2 paquet capture |
| 14 | 2 | Direction : 1 Windows rendu → moteur, 2 moteur → Windows capture |
| 16 | 8 | Génération non nulle de session |
| 24 | 8 | Numéro de séquence de paquet |
| 32 | 8 | Timestamp QPC, nul sans flag QPC_VALID |
| 40 | 4 | Nombre de frames : 1 à capacité négociée, maximum 4096 |
| 44 | 4 | Fréquence : 48000 |
| 48 / 50 | 2 / 2 | Canaux : 2 / format : 1 (PCM S32) |
| 52 | 4 | Taille payload : exactement frames × 8 |
| 56 | 4 | Flags : bit 0 discontinuité, bit 1 timestamp valide |
| 60 | 4 | Réservé : zéro |
| 64 | frames × 8 | PCM interleavé, y compris pour du silence |

Maximum : 32832 octets, en-tête inclus. Pas de frame partielle, paquet vide, données suffixes ni extension de version implicite. La capacité et la génération attendues sont **des données de session de confiance**, jamais reprises du paquet pour les valider. Un paquet rendu ne peut pas être accepté sur le trajet capture et vice versa, même si le PCM a le même format.

La génération sera attribuée à l'ouverture par le propriétaire de session et changera après retrait/reconnexion. Le validateur est sans état : il vérifie l'égalité de génération, pas l'authenticité du client, les doublons, l'ordre de séquence ou la monotonie QPC. Ces derniers contrôles appartiendront à la session de transport. Le timestamp décrit la première frame au point de production du paquet ; il ne signifie pas sa date de lecture/présentation dans Windows. Les deux directions peuvent signaler une discontinuité ou un timestamp valide. Un flag de timestamp avec valeur zéro est permis (valeur d'horloge initiale) ; il ne dispense pas le consommateur de connaître l'unité/fréquence QPC de la session.

## Conditions d'appel et limite de sécurité

Le buffer doit déjà être lisible et immuable pendant l'appel, par exemple un system buffer issu d'un futur IOCTL `METHOD_BUFFERED`. La taille fournie est la taille réellement disponible. Ce code ne vérifie pas une adresse utilisateur arbitraire et ne protège pas d'une modification concurrente : ne jamais l'appeler directement sur une zone utilisateur partagée sans protocole de copie/snapshot validé. Le validateur ne retourne pas de pointeur payload ; le consommateur peut utiliser l'offset 64 seulement après succès, avant la libération du même buffer.

Les IOCTL doivent vérifier ACL, droits, longueurs d'entrée/sortie et durée de vie des requêtes avant d'appeler ce code. Les valeurs opcode ici ne sont **pas** des codes IOCTL Windows. Le contrôle privé N2 implémente désormais les capacités, l'ouverture/fermeture de session, les transferts et les files bornées ; voir [son contrat et ses limites](driver/README.md#contrôle-privé-n2). Son intégration WaveRT est compilée avec le WDK mais attend les essais noyau réels. Le validateur seul ne prouve ni l'isolation multi-session, ni la sûreté du transport complet.

## Tests autonomes

Depuis la racine du dépôt, après `git rev-parse --show-toplevel` et `git status --short` :

```powershell
cmake -S native-windows-audio -B build_native_audio_protocol -G "Visual Studio 18 2026" -A x64
cmake --build build_native_audio_protocol --config Release
ctest --test-dir build_native_audio_protocol -C Release --output-on-failure
```

Les tests utilisent un paquet indépendant encodé octet par octet, compilent également le vocabulaire en C et vérifient les deux directions, la version, les formats, les bornes, les générations, l'alignement, les flags et les longueurs extrêmes. Le projet n'a aucune dépendance au SDK ASIO, au WDK ou à du matériel audio. Ces tests prouvent la validation des paquets, pas le transport ni le chargement d'un pilote.
