# Plan de Correction - Implémentation ASIO Correcte

> Plan historique remplacé par l’implémentation TimoxVasio à pilote unique.
> Ne pas utiliser ses commandes VASIO1–VASIO4 comme procédure d’installation.
> Le plan courant est [ici](docs/superpowers/plans/2026-10-03-vasio-256-physical-master-plan.md).

## Résumé du problème

L'implémentation actuelle utilise PortAudio (abstraction) au lieu d'implémenter directement l'interface IASIO du SDK Steinberg. **Résultat:** Les pilotes ne seront pas reconnus par Windows/DAW.

## Ce qui manque

### 1. Interface IASIO complète (CRITIQUE)
```cpp
// Actuellement: VirtualDriver (classe custom)
// Devrait: hériter de IASIO + implémenter 40+ méthodes
```

### 2. Callbacks bufferSwitch (CRITIQUE)
```cpp
// Actuellement: pas de callbacks
// Devrait: callbacks temps-réel synchronisés avec kernel audio
void bufferSwitchCallback(long index, ASIOBool processNow) {
    // Traiter audio ici
}
```

### 3. Double-buffering (CRITIQUE)
```cpp
// Actuellement: pas de ping-pong buffers
// Devrait: buffers[0] et buffers[1] alternés
```

### 4. DLL + Registration COM (CRITIQUE)
```cpp
// Actuellement: simple fichier exécutable
// Devrait: DLL enregistrée dans registre Windows avec CLSID
HKEY_LOCAL_MACHINE\SOFTWARE\Steinberg\ASIO\VASIO1
  ├─ CLSID = {GUID-UNIQUE}
  └─ InprocServer32 = C:\path\to\VASIO1.dll
```

## Solution proposée

### Phase 1: Vérifier l'état actuel (1 heure)

```bash
# Tester si VASIO1-4 apparaissent dans DAW
1. Compiler le code actuel
2. Ouvrir Reaper/MIXXX
3. Chercher VASIO1-4 dans l'audio settings
4. Si absent → implémentation manquante
```

### Phase 2: Implémenter IASIO correctement (4-6 heures)

**Fichier:** `src/vasio_driver_correct.cpp`

Contient:
- ✓ Interface IASIO complète
- ✓ Callbacks bufferSwitch
- ✓ Double-buffering
- ✓ DLL export
- ✓ Registration COM

**Compiler en DLL:**
```bash
cl.exe /LD vasio_driver_correct.cpp /I asiosdk/common /I asiosdk/host
# Génère: vasio_driver_correct.dll
```

### Phase 3: Enregistrement (1 heure)

**Créer 4 DLLs:** VASIO1.dll, VASIO2.dll, VASIO3.dll, VASIO4.dll

**Enregistrement (PowerShell admin):**
```powershell
# Copier DLLs
Copy-Item VASIO1.dll "C:\Program Files\Common Files\Steinberg\ASIO\"

# Enregistrer COM (via registration script)
regsvr32 "C:\Program Files\Common Files\Steinberg\ASIO\VASIO1.dll"

# Vérifier
Get-ItemProperty "HKLM:\SOFTWARE\Steinberg\ASIO" | Select-Object PS*
```

### Phase 4: Testing (2 heures)

**Checklist:**
- [ ] VASIO1-4 apparaissent dans Reaper/MIXXX
- [ ] Sélectionner VASIO1 → pas d'erreur
- [ ] Audio passe (silence = OK pour début)
- [ ] Pas de crashes
- [ ] Latence acceptable (< 50 ms)

## Ressources d'implémentation

### Documentation Steinberg
```
asiosdk/ASIO_SDK.txt          - Spec complète
asiosdk/host/asio.h           - Interface IASIO
asiosdk/host/asiodrivers.h    - Helper functions
```

### Exemples de référence
```
asiosdk/host/getsamplerate.cpp      - Lecture sample rate
asiosdk/host/createbuffers.cpp      - Gestion buffers
asiosdk/host/hostsample.cpp         - Host complet

External:
https://github.com/sadko4u/lsp-plugins/asio/
  → Implémentation ASIO réelle pour Linux/Wine
```

### Patterns clés

**1. Callbacks ultra-rapides**
```cpp
void bufferSwitch(long index, ASIOBool processNow) {
    // DOIT prendre < 1ms !!!
    // Pas d'allocation mémoire
    // Pas de stdio
    // Pas de locks
    for (int i = 0; i < numChannels; i++) {
        processChannel(buffers[i][index], bufferSize);
    }
    ASIOOutputReady();
}
```

**2. Synchronisation kernel**
```cpp
// Kernel audio appelle bufferSwitch
// Notre code doit retourner immédiatement
// Sinon → clicks, crashes
```

**3. Double-buffering**
```cpp
// Index alternates: 0 → 1 → 0 → 1 ...
// Pendant qu'on remplis buffer[1], kernel lit buffer[0]
// Évite les clicks d'overwrite
```

## Comparaison: PortAudio vs IASIO direct

| Aspect | PortAudio | IASIO direct |
|--------|-----------|-------------|
| Complexité | Bas (3-5 fichiers) | Élevée (40+ méthodes) |
| Latence | 10-50 ms | 1-5 ms |
| Contrôle | Limité | Complet |
| Features | Basiques | Avancées |
| Reconnaissance Windows | ❌ Pas garanti | ✓ 100% |
| DLLs requises | 1 | 4 (VASIO1-4) |
| Support DAW | Moyen | Excellent |

## Décision recommandée

**Pour production:** Implémenter IASIO correctement
- Raison: Compatibilité garantie
- Coût: 6-8 heures dev
- Bénéfice: Drivers professionnels

**Pour test rapide:** Utiliser PortAudio
- Raison: Fonctionne vite
- Coût: 2-3 heures dev
- Limitation: Pas reconnu par toutes les DAW

## Étapes concrètes

### 1. Compiler le code correct

```bash
cd asio

# Avec Visual Studio
cl.exe /LD src/vasio_driver_correct.cpp ^
  /I asiosdk/common ^
  /I asiosdk/host ^
  /Fe:VASIO1.dll

# Ou avec CMake
cmake -B build -G "Visual Studio 17 2022" -DASIO_SDK_PATH=asiosdk/
cmake --build build --config Release
```

### 2. Créer 4 DLLs

**Modifier pour chaque driver:**
```cpp
#define DRIVER_ID 1  // ou 2, 3, 4
// Compiler 4 fois → VASIO1.dll, VASIO2.dll, etc.
```

### 3. Enregistrer dans Windows

```powershell
# Admin PowerShell
$dllPath = "C:\...\VASIO1.dll"
[System.Runtime.InteropServices.RuntimeEnvironment]::SystemVersion

# Register
regsvr32 $dllPath

# Vérifier
reg query HKLM\SOFTWARE\Steinberg\ASIO
```

### 4. Tester dans DAW

1. Lancer Reaper
2. Options → Preferences → Audio Device
3. Chercher VASIO1-4
4. Sélectionner VASIO1
5. Audio input/output doit fonctionner

### 5. Corriger problèmes

**Erreur: "Cannot find VASIO1"**
→ DLL pas enregistrée
→ Vérifier registre
→ Relancer DAW

**Erreur: "VASIO1 not initialized"**
→ Init() pas appelé
→ Vérifier ASIOInit() dans callbacks

**Erreur: "Audio crackles"**
→ Buffer size incorrect
→ Callbacks trop lents
→ Réduire traitement

## Timeline

```
Phase 1 (Test):        1 heure    ← Où on en est
Phase 2 (IASIO impl):  4-6 heures
Phase 3 (Register):    1 heure
Phase 4 (Test DAW):    2 heures
─────────────────────
Total:                 8-10 heures
```

## Prochaines actions

1. ✅ **Audit fait** (ASIO_AUDIT.md)
2. ✅ **Code correct écrit** (vasio_driver_correct.cpp)
3. ⏭️ **Compiler et tester** (à faire)
4. ⏭️ **Corriger si nécessaire**
5. ⏭️ **Intégrer avec Electron GUI**

## Support

Si blocages:
- Consulter `asiosdk/ASIO_SDK.txt`
- Vérifier registre Windows
- Tester avec Reaper (meilleur support ASIO)
- Lire `asiosdk/host/hostsample.cpp` (référence)
