# Vérification des optimisations du chemin audio

Date : 2026-10-21. Base : `b7fe511`. Branche : `ccr-d306084d-ieaii9`.
Instructions : document du commit `768b198`, repris sur confirmation de l'utilisateur.
Cette première partie est rédigée et remise **avant toute modification du code**.

## Décisions préalables

| Proposition | Faisabilité et pertinence | Problèmes / décision |
|---|---|---|
| M1 — MMCSS | Oui : SDK Windows 10.0.26100.0, `um/avrt.h`, enum `AVRT_PRIORITY_CRITICAL = 2` et prototype `BOOL WINAPI(HANDLE, AVRT_PRIORITY)` vérifiés. Chargement dynamique sans lien avrt. La boucle actuelle n'élève pas sa priorité. Gain de robustesse plausible, pas de gain CPU garanti. | Implémenter. Charger avrt depuis System32, utiliser les types du SDK, traiter l'échec de priorité et révoquer sur le worker **avant** la publication d'arrêt et les tâches de nettoyage/reset. Conserver la priorité initiale lors du repli. |
| M2 — outputReady | Oui : aucun appel dans l'hôte physique. Contrat SDK confirmé, interrogation admise avant initialisation complète. Gain d'un bloc seulement si le pilote supporte ce mécanisme. | Implémenter : interrogation après `init`, avant création de buffers, état immuable pendant les callbacks. L'hôte physique ne lit ni ne publie actuellement `getLatencies`; aucune valeur publiée ne sera artificiellement diminuée. |
| M3 — conversion par bloc | Oui : dispatch actuellement répété par échantillon. Gain CPU à vérifier ; maintenir l'arithmétique scalaire et l'arrondi. Aucun changement du layout v9. | Implémenter avec comparaison bit à bit, types génériques conservés, buffers non alignés et tailles limites testés. Un pointeur sans argument `type` impose une enveloppe générique par format ; un type non supporté renverra `nullptr`. |
| M4 — suppressions mineures | Faisabilité à confirmer individuellement ; gain stéréo non établi. | Reporter conformément au dossier : pas de demande explicite de M4. |

Le classement par gain annoncé n'est pas démontré sur la machine. `bufferHealth` physique
mesure le callback matériel, pas directement l'attente du worker DLL. Une baisse de ses
compteurs n'est donc pas une preuve directe de M1. Le test de cadence sous charge reste utile.
`outputCopyTiming` inclura aussi `outputReady` après M2 : comparer M3 à M2, et non à la base.
Un loopback matériel avant/après nécessite une boucle câblée et une carte disponible ; aucun
gain de latence physique ni résultat sous charge n'est revendiqué ici sans cette mesure.

## Sources relues et corrections

- R1 : PDF local `asiosdk/Steinberg ASIO SDK 2.3.pdf`, page 46, section D Windows,
  relu avec pdftotext : exigence MMCSS confirmée. `asiosdk/common/asio.h:1031–1076`
  confirme outputReady, l'interrogation précoce, le retour `ASE_OK` et la condition sur le gain.
- R2 : `vendor/portaudio/src/hostapi/wasapi/pa_win_wasapi.c:1126–1145` confirme le
  chargement dynamique ; `:5187–5225` ne garantit pas CRITICAL (appel de priorité commenté).
  `src/common/pa_converters.c:176,332–350,1050–1078` confirme sélection et traitement par bloc,
  mais tronque à l'encodage. Nous conservons `llround`, la division signée et le filtrage des non-finis.
  Les fichiers `pa_ringbuffer.c` et `pa_process.c` restent des références hors périmètre.
- R3 : `vendor/portaudio/src/hostapi/wdmks/pa_win_wdmks.c:5560–5616` confirme le motif,
  y compris un repli si l'élévation échoue ; `DropThreadPriority` commence à 5603, pas 5598.
  Pas de reprise de `timeBeginPeriod`, inutile ici.
- R4 : `vendor/portaudio/src/hostapi/asio/pa_asio.cpp:988–991,2984–2985,3135–3136`
  confirme interrogation avant les buffers puis notification après traitement.
- R5 : [JACK2, JackMMCSS.cpp](https://github.com/jackaudio/jack2/blob/develop/windows/JackMMCSS.cpp),
  lignes 37–45, 59–69, 79–90 : chargement dynamique, Pro Audio et révocation confirmés.
  Correction : en-tête **LGPL-2.1-or-later**, pas GPL-2.0 ; priorité calculée depuis un argument,
  pas CRITICAL inconditionnel. Aucun code repris.
- R6 : [JUCE, juce_ASIO_windows.cpp](https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/native/juce_ASIO_windows.cpp),
  lignes 1253–1263 et 1353–1354 : interrogation avant les buffers, lecture des latences ensuite,
  notification après traitement. Pas d'élévation du callback ASIO identifiée dans ce fichier.
  La version courante renvoie à la licence JUCE 9 ; ne pas reprendre la qualification AGPL du dossier
  comme vérification juridique actuelle. Aucun code repris.
- R7 : [Mixxx, sounddeviceportaudio.cpp](https://github.com/mixxxdj/mixxx/blob/main/src/soundio/sounddeviceportaudio.cpp),
  lignes 996–1013 : priorité Qt hors Linux confirmée. Cela ne démontre pas que Mixxx soit
  effectivement moins sensible aux décrochages sur cette machine ; coexistence à mesurer.
- R8 : [SuperCollider, SC_PortAudio.cpp](https://github.com/supercollider/supercollider/blob/develop/server/scsynth/SC_PortAudio.cpp),
  appels `Pa_OpenStream` ligne 381 et `Pa_StartStream` ligne 415 ; aucun réglage de priorité
  Windows dans le fichier relu. Ce constat ne décrit pas tous ses backends.
- R9 : [Microsoft, MMCSS](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service),
  section Thread Priorities : catégorie haute pour Pro Audio et rétrogradation après quota confirmées.
  Ce n'est pas une garantie de délais ni de coexistence avec toutes les modifications de priorité hôte.
- R10 : discussion complémentaire non utilisée comme preuve ; le SDK local suffit au contrat.

## Référence avant modification

Racine et état Git contrôlés : clone demandé, arbre propre. Builds Release x64 du moteur,
du pilote, DriverProbe, DriverAudioProbe et des quatre cibles ci-dessous : succès.
MSBuild du pilote a nécessité une exécution autorisée hors bac à sable (E_ACCESSDENIED).

- AsioSampleConversionTests, AudioRoutingRuntimeTests, RoutingGraphTests : PASS.
- PhysicalAsioProbe : PASS, 24 entrées de registre. Ce probe **n'ouvre pas de carte**.
- DriverAudioProbe avec chemin absolu de la DLL construite : PASS, transport des canaux épars.
- DriverProbe : échec préexistant, min=max=preferred=2048, granularity=0.
  Son prédicat rejette toute granularité nulle, y compris une taille fixe annoncée par le moteur.
  Correction de l'annonce intermédiaire : ce n'est pas une assertion exigeant 256 trames.
  Aucun changement de configuration utilisateur ni de ce probe dans les optimisations.

## Journal d'exécution

Ordre : M1, M2, M3 ; un commit français par modification. Les probes existants restent inchangés,
conformément au dossier. Les résultats et empreintes des binaires seront ajoutés après chaque étape.

### M1 — terminée

Build Release x64 du pilote et probes : succès. AsioSampleConversionTests,
AudioRoutingRuntimeTests, RoutingGraphTests, PhysicalAsioProbe : PASS.
DriverAudioProbe : PASS en mode normal, reset, self-stop, self-stop-release,
release-before-reset, reset-stops et reinit-after-detach. `git diff --check` : succès.
Ces tests prouvent le transport et les sorties du worker, pas l'activation effective de MMCSS
ni une amélioration sous charge dans Mixxx/JUCE/SuperCollider.

Provenance (chemins relatifs à `C:/Users/timo/Documents/GitHub/TimoxVasio`) :

| État | Binaire | Horodatage UTC | SHA-256 |
|---|---|---|---|
| Base | `build_driver_110/Release/TimoxVasio.dll` | 2026-10-18T14:40:26.1314784Z | `9BB0DB27E0A498FDC9749AF5E6B510282129E082F1A33A0E9BD2F3A017FAE951` |
| Base | `build_codex_110/Release/TimoxVirtualAsioEngine.exe` | 2026-10-18T21:24:24.7568153Z | `39144511775B9FAACCE817DD5462E62F13886758AA92C410C330450B42D1FB44` |
| M1 | `build_driver_110/Release/TimoxVasio.dll` | 2026-10-21T05:55:02.9043923Z | `1E7CFCDF860F0FC18AE19DA3BFE742A1876A551A17FDD9772B3C3F1E5C6A9C08` |

### M2 — terminée

Interrogation après init ; notification après les sorties et le silence d'horloge,
avant la fin de la mesure outputCopyTiming. DLL virtuelle inchangée (`ASE_NotPresent`).
Build Release x64 du moteur et PhysicalAsioProbe : succès. AsioSampleConversionTests,
AudioRoutingRuntimeTests, RoutingGraphTests et PhysicalAsioProbe : PASS ; diff --check : succès.
Le support effectif et le gain loopback du pilote physique ne sont pas mesurés par ces tests.

| État | Binaire | Horodatage UTC | SHA-256 |
|---|---|---|---|
| M2 | `build_codex_110/Release/TimoxVirtualAsioEngine.exe` | 2026-10-21T05:56:08.0426729Z | `2A8CA1247310A72C4C8B4CB0F160C86A86A63DEF0C509B5319720F8D3E8845C9` |

### M3 — terminée

Sélection hors callback, huit chemins LSB spécialisés, dix enveloppes génériques.
Les fonctions scalaires existantes restent l'oracle inchangé. Test initial exécuté avec
sélecteurs vides : échec attendu « Missing block converter for supported type 16 ».
Après implémentation : PASS sur les 18 formats, 65 536 encodages 16 bits exhaustifs,
un million d'encodages PCM aléatoires par format et un million de motifs float aléatoires,
rampe, non-finis, zéros signés, limites et voisins des demi-LSB. Les valeurs ±0,5f
exercent aussi les égalités exactes d'arrondi. Buffers PCM décalés d'un octet, sentinelles,
lecture seule de l'entrée et tailles 0/1/7/255/256/257 vérifiées.

Build Release x64 moteur, PhysicalAsioProbe et tests : succès. Tests de conversion scalaire
et bloc, AudioRoutingRuntimeTests, RoutingGraphTests, PhysicalAsioProbe : PASS.
Benchmark informatif (encodage + décodage, 8 canaux × 256 trames × 1000 tours) :

| Format | Scalaire (ms) | Bloc (ms) | Rapport |
|---|---:|---:|---:|
| Int16LSB | 89,402 | 21,313 | 4,19× |
| Int24LSB | 99,413 | 27,060 | 3,67× |
| Int32LSB | 90,389 | 22,786 | 3,97× |
| Int32LSB24 | 88,952 | 25,525 | 3,48× |
| Float32LSB | 36,272 | 10,091 | 3,59× |

Ce microbenchmark ne mesure ni le moteur complet ni outputCopyTiming sur une carte.
Relecture indépendante : aucun défaut bloquant identifié ; tests directs des erreurs MMCSS,
de l'ordre outputReady et de la coexistence avec les hôtes réels encore absents.
Ces limites restent ouvertes pour la préversion demandée, sans promesse de gain matériel.

| État | Binaire | Horodatage UTC | SHA-256 |
|---|---|---|---|
| M3 | `build_codex_110/Release/TimoxVirtualAsioEngine.exe` | 2026-10-21T06:00:45.7764261Z | `E761D440EF6DAE185EF27F15353C76F689A098317E49C1D66A10FF3753B0C374` |

L'utilisateur demande ensuite une release avec suffixe `-test` : préparation de `1.2.7-test`
dans un commit de version distinct ; M4 reste exclue.
