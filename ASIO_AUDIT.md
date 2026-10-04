# Audit ASIO — notes de travail historiques

> **Attention : ce fichier contient des constats de dates et d’états différents.**
> Les sections anciennes ci-dessous ne décrivent pas toutes le pilote actuel; en
> particulier, l’affirmation qu’il n’existe pas d’interface `IASIO`, de DLL COM
> ou de callbacks est obsolète. Pour la synthèse courante, voir
> [README.md](README.md); pour l’évaluation du code actif et ses preuves, voir
> [ASIO_CONFORMITE.md](ASIO_CONFORMITE.md). Ne pas utiliser les anciennes
> recommandations de remplacement par PortAudio comme description du contrat
> actuel.

## Résumé courant

Le pilote actif est `TimoxVasio.dll`; il implémente `IASIO`, annonce 256 canaux
dans chaque direction et traite les canaux effectivement alloués par l’hôte.
Les probes locales vérifient la capacité, des allocations sparse et le transport
de callbacks. La conformité observée est documentée dans
[ASIO_CONFORMITE.md](ASIO_CONFORMITE.md). La validation complète d’un signal
depuis un hôte tiers jusqu’au périphérique matériel reste à effectuer.

---

## Notes d’audit et historique

Les constats qui suivent ont été conservés pour leur contexte et peuvent être
antérieurs au pilote et au moteur actuels. Ils ne sont pas le statut courant.

### État au 4 octobre 2026

## État courant

Le pilote et le moteur ont progressé depuis l’audit historique conservé plus bas.

- `TimoxVasio.dll` est un pilote ASIO x64 enregistré sous `HKLM\SOFTWARE\ASIO\TimoxVasio`. Le Mixxx installé est x64, version `2.6.0-beta` (commit `2.6-beta-402-ge1c1e5b72b`).
- Le probe PortAudio général énumère `TimoxVasio` avec 256 entrées et 256 sorties. Mixxx énumère le pilote via PortAudio mais cette révision le retire ensuite de sa liste, car son type `ChannelCount` sur 8 bits ne peut pas représenter 256.
- Le probe qui charge exactement `C:\Program Files\Mixxx\portaudio.dll` affiche la version PortAudio, puis reste dans `Pa_Initialize()` sans atteindre `Pa_GetDeviceCount()`. Ce résultat ne permet pas de conclure que Mixxx masque le pilote.
- Le relevé API effectué le 04/10/2026 confirme Renoise avec 64 entrées/sorties ouvertes, Ableton Live avec 2/2, et Mixxx avec 0/0 sur TimoxVasio. Les quatre routes actives relient les sorties stéréo de Renoise et d’Ableton aux sorties physiques SSL Monitor L/R. Ces routes sont présentes dans l’API; leur transmission audio dépend encore d’une validation avec un signal joué.
- Le moteur compile dans `build_engine_names_check`. Le moteur utilisé par les applications reste celui de `build_engine_vs2026_ninja`; il devra être redémarré pour activer les changements du nommage des sorties physiques.
- Le callback ASIO du pilote physique signale maintenant les changements de fréquence au worker du moteur. Celui-ci vérifie le signal au plus tard après son attente de 20 ms, arrête le moteur et invalide les capacités physiques jusqu’à une nouvelle application de configuration. Le build `TimoxVirtualAsioEngine` passe dans `build_engine_names_check`; le processus actif n’a pas été redémarré.
- La GUI React compile en production (`npm run react-build`). Les modifications verrouillent les réglages et les routes pendant `reconfiguring`; cette compilation ne valide pas le comportement en runtime.
- Le relevé de l’API du moteur actif confirme le SSL 12 avec 16 entrées et 8 sorties : les entrées ont des noms explicites, mais les sorties 3 à 8 sont encore `Out 3` à `Out 8`. La résolution de ces six libellés génériques est maintenant ajoutée pour la signature SSL 12 et le nom exact `SSL ASIO Driver 1`; elle conserve les noms non génériques transmis par le pilote. Les noms publiés seront `Line 3`, `Line 4`, `Headphone A L/R` et `Headphone B L/R`, rôles documentés par le [guide SSL 12](https://support.solidstatelogic.com/hc/en-gb/articles/5568765809309-SSL-12-User-Guide). Le build mis à jour passe dans `build_engine_names_check`; le moteur actif `build_engine_vs2026_ninja` n’a pas été redémarré, donc l’API continue de publier les anciens libellés.
- Un relevé WebSocket `audio.meter` de deux secondes sur les endpoints routés a rapporté `-120 dBFS` pour les sorties virtuelles et physiques. Les compteurs cumulatifs étaient Renoise 161 underruns/6 overruns et Ableton 1/0. Sans savoir si une lecture audio était en cours, ces niveaux ne permettent pas de conclure à une panne; la validation doit être répétée avec un son effectivement joué et les compteurs avant/après.

### Compatibilité Mixxx (4 octobre 2026)

- Le journal local identifie précisément Mixxx `2.6.0-beta`, commit `2.6-beta-402-ge1c1e5b72b`. Dans cette révision, `ChannelCount::value_t` est un `uint8_t` : 256 dépasse le maximum représentable (255), devient la sentinelle invalide, puis le périphérique est exclu lorsque ses nombres d’entrées et de sorties sont tous deux invalides. Ce diagnostic explique l’absence de TimoxVasio dans Mixxx après l’énumération PortAudio; ce n’est pas une limite 32/64 bits.
- L’analyse détaillée et le correctif source proposé sont dans [docs/mixxx-256-channel-compatibility.md](docs/mixxx-256-channel-compatibility.md) et [patches/mixxx/0001-audio-channel-count-support-256.patch](patches/mixxx/0001-audio-channel-count-support-256.patch). Le patch a été compilé dans une copie locale x64 Mixxx 2.7 (`build_mixxx_2.7_256/mixxx.exe`), mais ce binaire n’a pas été lancé ni installé. Le Mixxx 2.6 installé n’a reçu aucun changement. Ne pas réduire TimoxVasio à 255 canaux : cela violerait le contrat du pilote.
- Le code PortAudio inclus dans ce dépôt charge les pilotes ASIO durant son initialisation puis recueille leurs capacités; le probe générique voit TimoxVasio en 256/256. Le probe de la DLL exacte de Mixxx s’est bloqué dans `Pa_Initialize()` et ne permet pas une vérification indépendante de cette installation.
- Le journal `C:\Users\timo\AppData\Local\Mixxx\mixxx.log` a été modifié le 04/10/2026 à 13:49:14. Il montre la fermeture des Préférences audio à 13:46, mais aucune nouvelle trace d’inventaire PortAudio. Au même relevé, l’API TimoxVasio publie encore Mixxx avec 0 entrée et 0 sortie. L’ouverture/fermeture des Préférences n’a donc pas confirmé que Mixxx a retenu TimoxVasio. Le correctif source décrit ci-dessus n’est pas appliqué à l’installation. Un nouveau probe autonome avec la DLL de Mixxx est à éviter : le précédent est resté bloqué dans l’initialisation.

### Travail restant

1. Vérifier l’énumération et l’ouverture avec la build locale 2.7; pour utiliser TimoxVasio dans la version installée, porter le correctif sur son commit 2.6, reconstruire et installer Mixxx. La build locale 2.7 ne change pas cette installation. Le pilote TimoxVasio conserve 256 entrées et 256 sorties.
2. Après fermeture contrôlée des applications audio, installer et redémarrer le moteur mis à jour, puis confirmer les noms SSL 12 dans l’API et la matrice.
3. Confirmer le routage audio de bout en bout avec les applications hôtes.
4. L’interface Mixxx installée ne voit toujours pas TimoxVasio; le rafraîchissement de sa liste ne contourne pas la limite 256 de son `ChannelCount`.

## Archive d’audit antérieure au pilote courant

> Note d’audit historique, antérieure au pilote `TimoxVasio.dll`. Les exemples
> d’implémentation et les références aux quatre anciens pilotes ne sont pas le
> contrat courant. Voir [BUILD_DRIVERS.md](BUILD_DRIVERS.md) et [la spécification actuelle](docs/superpowers/specs/2026-10-03-vasio-single-driver-256-channel-design.md).

## Documentation Steinberg ASIO SDK

Source officielle: https://github.com/steinbergmedia/asiosdk

### Structure minimale requise

```cpp
// asio.h - Interface principale
struct IASIO {
    // Initialisation
    ASIOBool Init(void* sysHandle);
    void getDriverName(char* name);
    long getDriverVersion();
    
    // Capabilities
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels);
    ASIOError getBufferSize(long* minSize, long* maxSize, 
                           long* preferredSize, long* granularity);
    ASIOError canSampleRate(ASIOSampleRate sampleRate);
    ASIOError getSampleRate(ASIOSampleRate* sampleRate);
    ASIOError setSampleRate(ASIOSampleRate sampleRate);
    
    // Configuration
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                           long bufferSize, ASIOCallbacks* callbacks);
    ASIOError disposeBuffers();
    
    // Contrôle
    ASIOError start();
    ASIOError stop();
    ASIOError getLatencies(long* inputLatency, long* outputLatency);
    
    // I/O
    ASIOError outputReady();
    
    // Contrôle avancé
    ASIOError controlPanel();
    ASIOError future(long selector, void* opt);
    ASIOError dispose();
};
```

## Problèmes identifiés dans l'implémentation actuelle

### 1. ❌ Pas d'interface IASIO implémentée

**Problème:**
```cpp
// Actuellement: Wrapper PortAudio simplifié
class VirtualDriver {
    // Pas d'héritage de IASIO
    // Pas de méthodes ASIO requises
};
```

**Correct (ASIO SDK):**
```cpp
class VirtualASIODriver : public IASIO {
    ASIOError Init(void* sysHandle) override;
    ASIOError getChannels(long* in, long* out) override;
    ASIOError createBuffers(...) override;
    // ~40 méthodes obligatoires
};
```

### 2. ❌ Pas de registration COM/DLL

**Problème:**
L'ASIO SDK nécessite une DLL avec registration COM pour que Windows reconnaisse le driver.

**Correct:**
```cpp
// VASIO1.cpp - Export DLL
extern "C" EXPORT ASIODriver* (*asioCreateDriver)(void);

ASIODriver* asioCreateDriver(void) {
    return new VirtualASIODriver("VASIO1");
}

// Enregistrement dans registre (CLSID)
HKEY_LOCAL_MACHINE\SOFTWARE\Steinberg\ASIO\VASIO1
  → InprocServer32 = VASIO1.dll
  → CLSID = {GUID-UNIQUE}
```

### 3. ❌ Pas de callbacks audio temps-réel

**Problème:**
```cpp
// Actuellement pas de callbacks
ASIOError ProcessBuffer(float* input, float* output, long numSamples) {
    std::memcpy(output, input, ...);  // Trop lent !
}
```

**Correct (ASIO):**
```cpp
// Callback appelé par le kernel
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    // Traiter les buffers audio
    // MUST être fast (< 1ms)
    processAudio(doubleBufferIndex);
    
    // Notifier que output est ready
    ASIOOutputReady();
}
```

### 4. ❌ Pas de gestion buffer circulaire

**Problème:**
Pas de double-buffering (ping-pong) pour éviter les clicks.

**Correct:**
```cpp
struct BufferInfo {
    void* buffers[2];    // Double buffer
    long bufferSize;
    long currentIndex;   // 0 ou 1
};
```

### 5. ❌ Enregistrement registre trop simplifié

**Problème:**
```cpp
// Actuellement
RegSetValueExA(hKey, "VASIO1", 0, REG_SZ, "VirtualASIODriver", 18);
```

**Correct:**
```
HKEY_LOCAL_MACHINE\SOFTWARE\Steinberg\ASIO\VASIO1
  ├─ CLSID = {12345678-1234-1234-1234-123456789ABC}
  └─ [CLSID]
      └─ InprocServer32 = C:\path\to\VASIO1.dll
```

### 6. ❌ Pas de gestion des formats audio

**Problème:**
Hardcoding ASIOSTFloat32LSB.

**Correct:**
```cpp
ASIOError getChannelInfo(ASIOChannelInfo* info) {
    info->type = ASIOSTFloat32LSB;  // Ou Int24LSB, etc.
    info->isActive = ASIOTrue;
    strcpy(info->name, "VASIO1:1");
    return ASE_OK;
}
```

---

## Vérification contre exemples Steinberg

### Exemple 1: ASIO SDK Host Example

Source: `asiosdk/host/getsamplerate.cpp`

```cpp
// Bon pattern:
ASIOError ret = ASIOInit(&asioVersion);
if (ret == ASE_NotPresent) {
    // ASIO not installed
}

// Énumérer drivers
for (int i = 0; i < asioDrivers->getNumberOfDrivers(); i++) {
    char driverName[32];
    asioDrivers->getDriverName(i, driverName);
}

// Créer instance
ASIODriver* driver = asioDrivers->getCurrentDriver();
```

**Notre implémentation:** ❌ N'utilise pas `ASIOInit()`, pas de `asioDrivers`

### Exemple 2: ASIO SDK Tutorial

Pattern correct:

```cpp
// 1. Initialiser
ASIOCallbacks callbacks = {
    .bufferSwitch = bufferSwitchCallback,
    .sampleRateDidChange = sampleRateCallback,
    .asioMessage = messageCallback,
    .bufferSwitchTimeInfo = bufferSwitchTimeInfoCallback
};

// 2. Créer buffers
ASIOBufferInfo bufferInfos[numChannels];
for (int i = 0; i < numChannels; i++) {
    bufferInfos[i].isInput = ASIOFalse;
    bufferInfos[i].channelNum = i;
    bufferInfos[i].buffers[0] = malloc(bufferSize);
    bufferInfos[i].buffers[1] = malloc(bufferSize);
}

driver->createBuffers(bufferInfos, numChannels, bufferSize, &callbacks);

// 3. Démarrer
driver->start();

// 4. Callback en temps-réel
void bufferSwitchCallback(long index, ASIOBool processNow) {
    // ⚠️ MUST return < 1ms
    for (int i = 0; i < numChannels; i++) {
        float* buffer = (float*)bufferInfos[i].buffers[index];
        processAudio(buffer, bufferSize);
    }
    driver->outputReady();
}
```

**Notre implémentation:** ❌ Pas de callbacks, pas de bufferSwitch

---

## Résumé des écarts

| Point | Steinberg | Notre code | Status |
|-------|-----------|-----------|--------|
| Interface IASIO | ✓ Obligatoire | ❌ Manquant | **CRITIQUE** |
| Callbacks bufferSwitch | ✓ Obligatoire | ❌ Manquant | **CRITIQUE** |
| Double-buffering | ✓ Requis | ❌ Absent | **CRITIQUE** |
| Registration COM/DLL | ✓ Obligatoire | ❌ Simplifié | **CRITIQUE** |
| Methods ASIO (~40) | ✓ Obligatoire | ❌ ~15 seulement | **CRITIQUE** |
| Synchronisation kernel | ✓ Requis | ❌ Absent | **GRAVE** |
| Message callbacks | ✓ Recommandé | ❌ Absent | **Moyen** |
| Control panel | ✓ Recommandé | ❌ Absent | **Moyen** |

---

## Approche correcte

### Option A: Utiliser correctement l'ASIO SDK

```cpp
// VASIO1Driver.cpp - Implémentation IASIO complète
#include "asio.h"
#include "asiodrivers.h"

class VASIO1Driver : public IASIO {
    // ~40 méthodes à implémenter
    // Buffers circulaires
    // Callbacks temps-réel
    // Registration COM
};

// Exporter pour DLL
extern "C" {
    EXPORT ASIODriver* (*asioCreateDriver)(void) = createVASIO1Driver;
}
```

**Avantages:** ✓ Vrai driver ASIO, ✓ Full features
**Inconvénients:** ❌ Complexe (~2000 lignes), ❌ Gestion bas-niveau

### Option B: Continuer avec PortAudio (simplifié)

```cpp
// Garder PortAudio comme backend
// Utiliser wrapper ASIO officiel de PortAudio
// Enregistrer via helper ASIO
```

**Avantages:** ✓ Simple, ✓ Marche rapidement
**Inconvénients:** ❌ Pas de vrai driver ASIO, ❌ Latence plus haute

### Option C: Utiliser VirtualAudio (recommandé)

Utiliser une bibliotèque dédiée aux drivers virtuels:
- VirtualAudio.exe (VB-Audio)
- LoopMIDI (MIDI, pattern similaire)
- VoiceMeeter (source partielle disponible)

**Avantages:** ✓ Éprouvé, ✓ Professionnel
**Inconvénients:** ❌ Moins d'apprentissage

---

## Décision recommandée

### Pour une implémentation CORRECTE:

**Approche hibride:**

1. **Backend C++:** PortAudio (simple, fonctionne)
2. **Frontend Electron/React:** ✓ Déjà fait, bon design
3. **Driver ASIO:** Utiliser wrapper ASIO officiel de PortAudio

**Raison:** 
- PortAudio a un backend ASIO officiel
- Pas besoin de 40 méthodes IASIO
- Évite la complexité DLL/COM
- Reste compatible ASIO

---

## Vérifications à faire

### 1. L'ASIO SDK est-il vraiment nécessaire?

**Réponse:** Oui, mais PortAudio l'intègre déjà.

✓ PortAudio → ASIO SDK (interne)
✓ Notre code → PortAudio (wrapper)

### 2. Les drivers virtuels apparaîtront-ils dans les DAW?

**Actuellement:** Problématique

Besoin:
- [ ] Enregistrement COM correct
- [ ] CLSID unique par driver
- [ ] DLL signée (ou désactiver SmartScreen)
- [ ] Redémarrage Windows

### 3. Latence acceptable?

**PortAudio:** 10-20 ms (acceptable)
**ASIO natif:** 1-5 ms (meilleur)

---

## Plan de correction

### Phase 1: Vérification (rapide)
- [ ] Tester si VASIO1-4 apparaissent dans Reaper
- [ ] Vérifier registre Windows
- [ ] Tester avec audio simple

### Phase 2: Utiliser l'ASIO SDK correctement
- [ ] Lire `asiosdk/host/asiodrivers.cpp` complet
- [ ] Implémenter les 40 méthodes IASIO
- [ ] Gérer callbacks bufferSwitch
- [ ] Double-buffering proper

### Phase 3: Packaging
- [ ] Signer DLL (optionnel)
- [ ] Installer script
- [ ] Testing multi-DAW

---

## Ressources officielles

**Documentation ASIO:**
- https://github.com/steinbergmedia/asiosdk
- `asiosdk/ASIO_SDK.txt` - Spec complète
- `asiosdk/host/asio.h` - Interface principale
- `asiosdk/host/asiodrivers.cpp` - Implémentation exemple

**Exemples:**
- `asiosdk/host/getsamplerate.cpp` - Lecture sample rate
- `asiosdk/host/createbuffers.cpp` - Gestion buffers
- `asiosdk/host/hostsample.cpp` - Host complet

**Community:**
- ASIO SDK issues: https://github.com/steinbergmedia/asiosdk/issues
- Reaper ASIO testing
- MIXXX ASIO implementation

---

## Conclusion

**Notre implémentation actuelle:**
- ✓ Bonne interface Electron/React
- ✓ Communication C++ saine
- ❌ Pas conforme ASIO SDK
- ❌ Drivers ne seront pas reconnus par Windows

**Recommandation:**
Utiliser PortAudio officiellement avec son backend ASIO,
plutôt que coder from scratch l'interface IASIO.

**Prochaine étape:**
Vérifier si les drivers apparaissent dans une DAW réelle.
Si non → implémenter correctement les callbacks ASIO.
