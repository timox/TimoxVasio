# Optimisation du chemin audio TimoxVasio : dossier de vérification pour l'agent codeur

Date : 2026-10-20
Branche de travail : `ccr-d306084d-ieaii9`
Auteur : Claude, session d'analyse du dépôt `timox/TimoxVasio`
Destinataire : agent codeur chargé de vérifier, puis d'implémenter si rien ne s'y oppose

## 1. Mission confiée à l'agent codeur

Ce document propose quatre modifications du traitement audio, classées par gain décroissant.
Chaque constat renvoie à une ligne du dépôt et chaque choix de conception renvoie à une
source externe listée en section 9. Avant toute écriture de code, tu dois :

1. **Vérifier la faisabilité** : chaque modification compile-t-elle dans les cibles existantes
   (`TimoxVasio.dll` via `cmake/CMakeLists_VASIO_Drivers.txt`, `TimoxVirtualAsioEngine.exe` via
   `CMakeLists.txt`) sans nouvelle dépendance de lien et sans changer le format de mémoire partagée v9 ?
2. **Vérifier la pertinence** : le gain annoncé est-il réel sur la configuration de référence
   (carte physique, 48 kHz, 256 trames, un client stéréo) et mesurable avec les compteurs déjà présents ?
3. **Vérifier le bien-fondé** : relis chaque source citée. Si une citation est inexacte, si le code
   référencé ne dit pas ce que j'affirme, ou si une ligne du dépôt a changé, signale-le.
4. **Rédiger le rapport** décrit en section 8, dans tous les cas, même si tout est validé.
5. **N'implémenter** qu'après avoir remis le rapport, et seulement les modifications sans problème
   bloquant, dans l'ordre M1, M2, M3. M4 n'est implémentée que sur demande explicite.

Règles du dépôt à respecter : `AGENTS.md` (racine confirmée par `git rev-parse --show-toplevel`,
provenance des binaires par chemin, horodatage et SHA-256, aucune modification d'interface).
Un commit par modification, message en français, aucun identifiant de modèle dans les commits.

## 2. Périmètre et chemin chaud analysé

Le chemin audio traverse trois composants, voir `docs/ARCHITECTURE.md` :

| Étape | Fichier | Fonction | Thread |
|---|---|---|---|
| Callback hôte côté application | `src/vasio_driver.cpp:421` | `VASIODriver::callbackLoop` | `std::thread` créé en `src/vasio_driver.cpp:177` |
| Anneau partagé sans verrou | `src/audio_transport.cpp:463` et `:483` | `AudioClientMapping::Write` / `Read` | les deux côtés |
| Callback du pilote physique | `src/physical_asio_host.cpp:137` | `Session::Process` | thread du pilote matériel |
| Graphe de routage | `src/audio_routing_runtime.cpp` | `AudioRoutingRuntime::Process` | thread du pilote matériel |
| Mixage SSE2 | `src/routing_graph.cpp` | `RoutingGraph::Process` | thread du pilote matériel |

Hors périmètre : l'interface Electron, l'API HTTP, le format de mémoire partagée v9
(`src/audio_transport_layout.h`), la fréquence et la taille de bloc.

## 3. M1 : priorité MMCSS « Pro Audio » du worker de callback de la DLL

### Constat

`VASIODriver::start` crée le worker avec `std::thread` (`src/vasio_driver.cpp:177`). La boucle
`callbackLoop` (`src/vasio_driver.cpp:421`) appelle `bufferSwitch` ou `bufferSwitchTimeInfo` de
l'hôte sur ce thread. Aucun appel à `SetThreadPriority`, `AvSetMmThreadCharacteristics` ou MMCSS
n'existe dans `src/`, `include/` ni `tools/` (recherche `grep -rn "SetThreadPriority\|AvSetMm"`).
Tout le DSP de l'application hôte s'exécute donc à priorité normale.

### Sources

- **Exigence du SDK ASIO** [R1], page 46, section « D. Platform/OS Differences, Windows » :
  « On Windows Vista or any newer Windows version, ASIO driver threads must be in the "Pro Audio"
  class of MMCSS and their priority set to "CRITICAL". [...] It lies within the sole responsibility
  of the ASIO driver to set the priorities of the threads it owns. An ASIO host shall by no means
  alter these priorities. »
- **Les hôtes libres s'y fient** : JUCE n'élève pas la priorité du thread ASIO dans
  `juce_ASIO_windows.cpp` [R6] ; SuperCollider non plus dans `SC_PortAudio.cpp` [R8]. Mixxx
  compense seul avec `QThread::TimeCriticalPriority` dans `callbackProcessClkRef` [R7], ce qui
  explique qu'il soit moins sensible que les autres hôtes.
- **Implémentation de référence, déjà dans le dépôt** : PortAudio, `vendor/portaudio/src/hostapi/wdmks/pa_win_wdmks.c`,
  fonction `BumpThreadPriority` (lignes 5560 à 5595) [R3] : `AvSetMmThreadCharacteristics("Pro Audio", &dwTask)`
  puis `AvSetMmThreadPriority(hAVRT, PA_AVRT_PRIORITY_CRITICAL)`, repli sur
  `SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL)` si MMCSS échoue, et `DropThreadPriority`
  (ligne 5598) qui remet `AVRT_PRIORITY_NORMAL` puis `AvRevertMmThreadCharacteristics`.
- **Chargement dynamique d'avrt.dll** : `vendor/portaudio/src/hostapi/wasapi/pa_win_wasapi.c`,
  fonction `SetupAVRT` (lignes 1126 à 1145) [R2] : `LoadLibraryA("avrt.dll")` puis `GetProcAddress`
  de `AvSetMmThreadCharacteristicsA`, `AvRevertMmThreadCharacteristics`, `AvSetMmThreadPriority`.
  JACK2 fait de même dans `windows/JackMMCSS.cpp` [R5] avec la classe « Pro Audio ».
- **Documentation Microsoft** [R9] : la classe « Pro Audio » est la catégorie haute réservée à
  l'audio professionnel ; un thread qui dépasse son quota CPU est rétrogradé.

### Modification proposée

Dans `src/vasio_driver.cpp`, en tête de `callbackLoop`, après `currentCallbackDriver = this;` :

```cpp
// Exigence SDK ASIO 2.3 p.46 : thread de bufferSwitch en classe MMCSS "Pro Audio", priorité CRITICAL.
// Motif repris de PortAudio pa_win_wdmks.c BumpThreadPriority et pa_win_wasapi.c SetupAVRT.
HMODULE avrt = LoadLibraryW(L"avrt.dll");
HANDLE mmcssTask = nullptr;
using SetCharacteristicsFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
using SetPriorityFn = BOOL(WINAPI*)(HANDLE, int /*AVRT_PRIORITY*/);
using RevertFn = BOOL(WINAPI*)(HANDLE);
SetPriorityFn setPriority = nullptr;
RevertFn revert = nullptr;
if (avrt) {
    auto setCharacteristics = reinterpret_cast<SetCharacteristicsFn>(
        GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
    setPriority = reinterpret_cast<SetPriorityFn>(GetProcAddress(avrt, "AvSetMmThreadPriority"));
    revert = reinterpret_cast<RevertFn>(GetProcAddress(avrt, "AvRevertMmThreadCharacteristics"));
    DWORD taskIndex = 0;
    if (setCharacteristics && revert) mmcssTask = setCharacteristics(L"Pro Audio", &taskIndex);
    if (mmcssTask && setPriority) setPriority(mmcssTask, 2 /*AVRT_PRIORITY_CRITICAL*/);
}
if (!mmcssTask) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
```

En sortie de boucle, avant `currentCallbackDriver = nullptr;` :

```cpp
if (mmcssTask && revert) revert(mmcssTask);
else SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
if (avrt) FreeLibrary(avrt);
```

Aucun ajout à `target_link_libraries` de `cmake/CMakeLists_VASIO_Drivers.txt:44` : avrt.dll est
chargée dynamiquement, comme PortAudio, pour ne pas ajouter d'import à une DLL injectée dans des
processus tiers.

### Points à vérifier

- Les constantes `AVRT_PRIORITY_CRITICAL` et le prototype de `AvSetMmThreadPriority` dans `<avrt.h>`
  du SDK Windows. Préférer inclure `<avrt.h>` pour les types et garder le chargement dynamique.
- `callbackLoop` est `noexcept` : aucune des fonctions ajoutées ne lance.
- Le chemin de sortie de la boucle passe par `reapCallbackWorker` et `deliverResetRequest` ;
  la révocation doit se faire sur le thread du worker lui-même, avant ces appels.
- Impact sur les hôtes qui règlent déjà leur priorité (Mixxx [R7]) : vérifier que `TimeCriticalPriority`
  de Qt et la classe MMCSS coexistent sans erreur. Microsoft indique qu'un thread MMCSS garde sa
  priorité tant qu'il respecte son quota [R9].
- Aucun échantillon ne change : les tests `tests/driver_probe.cpp` et `tests/driver_audio_probe.cpp`
  doivent passer tels quels.
- Mettre à jour `docs/ASIO_CONFORMITE.md` et sa version anglaise : la ligne « Callbacks et position »
  du tableau doit mentionner l'exigence MMCSS du SDK et le comportement implémenté.

### Critères de non-régression

Aucune modification du flux d'échantillons. Les compteurs `delayedStarts` et `overruns` de
`bufferHealth` (`docs/ASIO_BUFFER_HEALTH.md`) ne doivent pas augmenter ; ils devraient baisser sous
charge. Les journaux de cadence de `vasio-probe cadence` (`tools/vasio-probe/main.cpp:54`) servent de
mesure avant et après.

## 4. M2 : appel de `outputReady()` sur le pilote physique

### Constat

Le moteur est hôte ASIO du pilote matériel. Dans `src/physical_asio_host.cpp`, `Session::Process`
(ligne 137) décode les entrées, appelle le graphe, encode les sorties (lignes 172 à 179), remplit le
buffer d'horloge (ligne 180) puis retourne. Aucun appel à `outputReady` n'existe dans `src/`
(`grep -rn "outputReady" src/` ne renvoie que `src/vasio_driver.cpp`, où notre propre DLL répond
`ASE_NotPresent`, ce qui reste correct).

### Sources

- **SDK ASIO**, `asiosdk/common/asio.h:1031` à `:1049` [R1] : « when the host finds out that
  ASIOOutputReady() returns true, it will issue this call whenever it completed output processing.
  then the driver can convert the host data directly to the dma buffer to be played next, reducing
  output latency by one block. »
- **PortAudio**, `vendor/portaudio/src/hostapi/asio/pa_asio.cpp` [R4] : dans `LoadAsioDriver`
  (ligne 943), `if( ASIOOutputReady() == ASE_OK ) driverInfo->postOutput = true;` (ligne 988) ;
  dans `bufferSwitchTimeInfo` (ligne 2891), après l'écriture des sorties :
  `if( theAsioStream->postOutput ) ASIOOutputReady();` (lignes 2985 et 3136).
- **JUCE**, `juce_ASIO_windows.cpp` [R6] : `postOutput = (asioObject->outputReady() == 0);` dans
  `openDevice`, puis `if (postOutput) asioObject->outputReady();` en fin de `processBuffer`.

### Modification proposée

Dans `struct PhysicalAsioHost::Session` (`src/physical_asio_host.cpp:102`), ajouter
`bool postOutput = false;`. Dans `PhysicalAsioHost::start`, après la vérification des buffers et
avant `session.driver->start()` (ligne 574) :

```cpp
// SDK ASIO asio.h:1031 ; motif PortAudio pa_asio.cpp:988 et JUCE openDevice.
session.postOutput = session.driver->outputReady() == ASE_OK;
```

Dans `Session::Process`, après le `memset` du buffer d'horloge (ligne 182) et avant la mesure
`sampleOutputCopy` :

```cpp
if (postOutput) driver->outputReady();
```

### Points à vérifier

- Ordre d'interrogation : PortAudio interroge avant `createBuffers`, JUCE dans `openDevice`.
  Vérifier dans le SDK [R1] si l'appel est admis entre `createBuffers` et `start`, ou le déplacer
  juste après `init` dans `PhysicalAsioHost::open`.
- Le pilote qui ne gère pas la fonction répond `ASE_NotPresent` à l'interrogation et n'est jamais
  rappelé : pas de chemin de régression.
- `Session::Process` est `noexcept` et s'exécute sur le thread du pilote ; `outputReady` y est appelé
  par PortAudio et JUCE dans le même contexte, c'est le contrat attendu.
- Latence annoncée : certains pilotes réduisent la valeur de `getLatencies` une fois l'hôte déclaré
  `outputReady`. Vérifier si `PhysicalAsioHost` lit les latences après ce point et si l'API les publie.

### Critères de non-régression

Sur un pilote sans support : comportement identique. Sur un pilote avec support : mesurer la boucle
physique avec `vasio-probe loopback` (`tools/vasio-probe/main.cpp:54`) avant et après ; le gain attendu
est un bloc de sortie, soit 5,3 ms à 256 trames et 48 kHz.

## 5. M3 : conversion PCM par bloc, sélection du format une fois par canal

### Constat

`Session::Process` appelle `AsioSampleConversion::Decode` par échantillon (`src/physical_asio_host.cpp:158`)
et `AsioSampleConversion::Encode` par échantillon (`src/physical_asio_host.cpp:178`). Chaque appel
(`src/asio_sample_conversion.cpp:73` et `:98`) refait le test d'endianness, le switch de type, une boucle
d'octets, une division en double et, à l'encodage, un `std::llround` (`src/asio_sample_conversion.cpp:121`).
Le coût est linéaire en voies physiques routées × trames, deux fois par callback.

### Sources

- **PortAudio**, `vendor/portaudio/src/common/pa_converters.c` [R2] :
  `PaUtil_SelectConverter` (ligne 176) choisit un pointeur de fonction une fois par flux et par couple
  de formats ; `Float32_To_Int32` (ligne 332) et `Int24_To_Float32` (ligne 1050) bouclent sur un bloc
  entier avec un pas source et un pas destination, assemblent l'entier dans les bits hauts et multiplient
  par la constante `const_1_div_2147483648_` (ligne 328), sans switch ni boucle d'octets par échantillon.
- **Différence d'arithmétique à ne pas reprendre** : PortAudio tronque avec `(PaInt32) scaled`
  (`pa_converters.c:344`) alors que le dépôt arrondit avec `std::llround`. Reprendre la structure,
  pas l'arithmétique, sinon le LSB change.

### Modification proposée

Dans `include/asio_sample_conversion.h` et `src/asio_sample_conversion.cpp` :

```cpp
using DecodeBlockFn = void (*)(float* dst, const std::uint8_t* src, std::uint32_t frames) noexcept;
using EncodeBlockFn = void (*)(std::uint8_t* dst, const float* src, std::uint32_t frames) noexcept;
static DecodeBlockFn SelectDecoder(ASIOSampleType type) noexcept;
static EncodeBlockFn SelectEncoder(ASIOSampleType type) noexcept;
```

Chemins spécialisés : `ASIOSTInt16LSB`, `ASIOSTInt24LSB`, `ASIOSTInt32LSB`, `ASIOSTInt32LSB16`,
`ASIOSTInt32LSB18`, `ASIOSTInt32LSB20`, `ASIOSTInt32LSB24`, `ASIOSTFloat32LSB`. Tous les autres types
retombent sur une boucle générique qui appelle `Decode` ou `Encode` par échantillon.

Chaque boucle spécialisée garde **exactement** les formules actuelles :

- décodage : lecture de l'entier par `std::memcpy` ou assemblage d'octets, extension de signe comme
  aux lignes 90 à 92, division entière `value /= (1 << (32 - bits))` pour les formats 32 bits tronqués
  (division, pas décalage, pour conserver la troncature vers zéro des négatifs), puis
  `static_cast<float>(static_cast<double>(value) / std::ldexp(1.0, bits - 1))` ;
- encodage : `isfinite` puis `clamp` comme aux lignes 100 et 101, `sample <= -1.0f ? minValue :
  std::llround(static_cast<double>(sample) * maxValue)` comme à la ligne 121, décalage
  `<< (32 - bits)` pour les formats tronqués, écriture little-endian par `std::memcpy`.

Dans `Session` : `std::vector<DecodeBlockFn> inputDecoders; std::vector<EncodeBlockFn> outputEncoders;`
remplis dans `PhysicalAsioHost::start` à côté des `ASIOChannelInfo`. Dans `Session::Process`, les
boucles des lignes 157 à 159 et 177 à 179 deviennent un appel par canal.

### Points à vérifier

- `std::llround` reste scalaire : le gain vient de la suppression du dispatch et de la boucle d'octets.
  Ne pas remplacer `llround` par `_mm_cvtpd_epi32` dans cette étape : l'arrondi passerait de « demi
  vers l'extérieur » à « demi vers le pair » et modifierait le LSB sur les égalités exactes. Cette
  seconde étape est possible mais doit être décidée séparément, documentée comme changement de
  comportement.
- Big-endian et `Float64` restent sur le chemin générique : aucun pilote x64 courant ne les expose,
  inutile de les optimiser.
- `physicalOutputsPrecleared` et le buffer d'horloge (`src/physical_asio_host.cpp:180`) ne sont pas
  concernés.

### Test de non-régression à ajouter

`tests/asio_sample_conversion_block_tests.cpp`, cible CMake `AsioSampleConversionBlockTests` à côté
de `AsioSampleConversionTests` (`CMakeLists.txt`, cible existante compilant `src/asio_sample_conversion.cpp`) :

- pour chaque type spécialisé, comparer `SelectDecoder(type)` à `Decode` par échantillon sur un
  balayage exhaustif des 65 536 valeurs en 16 bits, et sur une rampe plus 1 million de valeurs
  pseudo-aléatoires en 24 et 32 bits ; égalité exigée des bits float (`memcmp`) ;
- pour chaque type spécialisé, comparer `SelectEncoder(type)` à `Encode` sur une rampe de
  `-1.5f` à `1.5f` incluant `NaN`, `±inf`, `±0`, `±1`, les demi-LSB exacts et 1 million de valeurs
  pseudo-aléatoires ; égalité exigée des octets ;
- vérifier que les types non spécialisés renvoient la boucle générique et donnent le même résultat.

Les tests existants `tests/asio_sample_conversion_tests.cpp` restent inchangés.

### Critères de non-régression

Bit-exactitude prouvée par le test ci-dessus. Mesure avant et après avec `outputCopyTiming`
(`src/physical_asio_host.cpp:135`, publié par l'API de performance) sur une carte à 8 voies ou plus.

## 6. M4 : suppressions de travail mineures, sur demande seulement

Ces points sont corrects mais marginaux en stéréo. Ne les implémenter que si demandé.

- `src/vasio_driver.cpp:471` : `std::fill(transportBlock_)` avant le réentrelacement. Chaque slot de
  sortie `0..outputStride_-1` est écrit par exactement un canal actif, car `SetActiveChannels`
  (`src/audio_transport.cpp:294` à `:317`) construit la table compacte à partir du même masque que
  `channelBuffers_`. Le remplissage peut être limité aux slots sans canal, qui n'existent pas aujourd'hui.
- `src/audio_routing_runtime.cpp`, boucle finale de `Process` : même redondance sur `clientInput`,
  à limiter aux slots dont `inputEndpointIndices[slot] == -1`.
- Mètres de crête dans `AudioRoutingRuntime::Process` : passer en SSE2 sur le modèle de
  `RoutingGraph::Process` (`src/routing_graph.cpp`), avec `andnot` du bit de signe, `max_ps` et
  conservation du filtre `isfinite`.
- Fusion de la lecture du ring et du désentrelacement dans la DLL sur le modèle des pas source et
  destination de PortAudio `pa_process.c` [R2], pour supprimer `transportBlock_`. Gain faible en
  stéréo, à garder pour plus tard.

## 7. Modifications écartées, avec justification

- **Anneau planaire par slot (layout v10)** : supprimerait deux `memcpy` et les deux entrelacements
  par direction, mais change `src/audio_transport_layout.h`, oblige à reconstruire DLL et moteur
  ensemble et casse la cohabitation avec une 1.2.6 encore chargée dans un hôte (voir le chemin
  versionné dans `AGENTS.md`). À réserver à une version majeure.
- **Latence structurelle d'un bloc par direction** : le client écrit le bloc N pendant que le moteur
  consomme le bloc N-1 (`src/audio_routing_runtime.cpp`, `ReadClientOutput` puis `WriteEngineInput`
  et `SignalClient`). La supprimer exigerait un callback client synchrone dans le callback physique,
  inter-processus. Ce n'est pas un changement sans régression.
- **Anneau lui-même** : `vendor/portaudio/src/common/pa_ringbuffer.c` [R2] est le même anneau SPSC
  sans verrou que `AudioClientMapping::Write` et `Read`. Rien à y gagner.

## 8. Rapport attendu de l'agent codeur

Fichier : `docs/plans/2026-10-20-optimisation-chemin-audio-rapport.md`, en français, remis avant
toute implémentation. Pour chaque modification M1 à M4 :

| Rubrique | Contenu attendu |
|---|---|
| Faisabilité | Oui, Non ou Sous condition. Preuve : sortie de build, lignes du dépôt relues, en-têtes du SDK Windows vérifiés. |
| Pertinence | Gain mesurable ou non sur la configuration de référence, avec le compteur utilisé. |
| Bien-fondé | Chaque source R1 à R9 relue : confirme, nuance ou contredit, avec la ligne exacte. |
| Problèmes | Tout écart entre ce document et le code réel, toute hypothèse fausse, tout risque non listé. |
| Décision | Implémenter, Reporter ou Abandonner, en une phrase. |

En cas de problème bloquant sur une modification, explique-le sans l'implémenter et passe à la
suivante. En cas de doute sur une interprétation du SDK ASIO, cite la page du PDF [R1] et la ligne
de `asio.h`. Si tout est validé, le rapport tient en une page et l'implémentation suit, un commit
par modification, sur la branche `ccr-d306084d-ieaii9`.

Vérifications minimales avant chaque commit : build Release x64 des cibles touchées,
`AsioSampleConversionTests`, `AsioSampleConversionBlockTests` (M3), `AudioRoutingRuntimeTests`,
`RoutingGraphTests`, `PhysicalAsioProbe` si une carte est disponible. Consigner les chemins,
horodatages et SHA-256 des binaires produits, conformément à `AGENTS.md`.

## 9. Licences des sources reprises

- PortAudio [R2] : licence permissive de type MIT, voir `vendor/portaudio/LICENSE.txt`, compatible
  avec le GPL-3.0-only du dépôt. Les motifs `SetupAVRT` et `BumpThreadPriority` peuvent être repris
  avec mention d'origine dans le commentaire.
- JACK2 [R5] : GPL-2.0 ou ultérieure pour `windows/JackMMCSS.cpp`, compatible avec GPL-3.0. Cité
  comme confirmation du motif, pas repris.
- JUCE [R6] : AGPL-3.0 ou licence commerciale. Cité pour l'idée uniquement, aucun code repris.
- Mixxx [R7] et SuperCollider [R8] : GPL. Cités comme comportement d'hôte, aucun code repris.
- SDK ASIO [R1] : licence Steinberg, déjà acceptée par le dépôt (`asiosdk/LICENSE.txt`).

## 10. Références

- [R1] Steinberg, *ASIO SDK 2.3*, `asiosdk/Steinberg ASIO SDK 2.3.pdf`, page 46, section
  « D. Platform/OS Differences, Windows », exigence MMCSS « Pro Audio » / « CRITICAL » ;
  `asiosdk/common/asio.h` lignes 1031 à 1049, contrat de `ASIOOutputReady`.
- [R2] PortAudio, copie vendue dans `vendor/portaudio` :
  `src/hostapi/wasapi/pa_win_wasapi.c` lignes 1126 à 1145 (`SetupAVRT`) et 5187 à 5225
  (`MMCSS_activate`) ; `src/common/pa_converters.c` lignes 176, 328, 332 à 350, 1050 à 1078 ;
  `src/common/pa_ringbuffer.c` ; `src/common/pa_process.c`.
  Dépôt amont : https://github.com/PortAudio/portaudio
- [R3] PortAudio, `vendor/portaudio/src/hostapi/wdmks/pa_win_wdmks.c` lignes 5560 à 5612,
  `BumpThreadPriority` et `DropThreadPriority`.
  https://github.com/PortAudio/portaudio/blob/master/src/hostapi/wdmks/pa_win_wdmks.c
- [R4] PortAudio, `vendor/portaudio/src/hostapi/asio/pa_asio.cpp` lignes 943, 988, 2891, 2985, 3136,
  `postOutput` et `ASIOOutputReady`.
  https://github.com/PortAudio/portaudio/blob/master/src/hostapi/asio/pa_asio.cpp
- [R5] JACK2, `windows/JackMMCSS.cpp`, chargement dynamique d'avrt.dll et classe « Pro Audio ».
  https://github.com/jackaudio/jack2/blob/develop/windows/JackMMCSS.cpp
- [R6] JUCE, `modules/juce_audio_devices/native/juce_ASIO_windows.cpp`, `postOutput` dans
  `openDevice` et `processBuffer` ; aucun réglage de priorité du thread ASIO.
  https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/native/juce_ASIO_windows.cpp
- [R7] Mixxx, `src/soundio/sounddeviceportaudio.cpp`, `callbackProcessClkRef`,
  `QThread::currentThread()->setPriority(QThread::TimeCriticalPriority)` hors Linux.
  https://github.com/mixxxdj/mixxx/blob/main/src/soundio/sounddeviceportaudio.cpp
- [R8] SuperCollider, `server/scsynth/SC_PortAudio.cpp`, aucun réglage de priorité Windows.
  https://github.com/supercollider/supercollider/blob/develop/server/scsynth/SC_PortAudio.cpp
- [R9] Microsoft, *Multimedia Class Scheduler Service*.
  https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service
- [R10] Forum Steinberg SDK, discussion sur l'exigence MMCSS pour les pilotes ASIO et les pratiques
  de Cubase, Pro Tools et Reaper. https://sdk.steinberg.net/viewtopic.php?p=1809

Toutes les lignes du dépôt citées correspondent au commit `3f378ef` de `main`, base de la branche
`ccr-d306084d-ieaii9` au 2026-10-20.
