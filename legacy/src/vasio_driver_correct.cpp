// Ancien prototype ASIO six canaux, non référencé par les cibles de build.
// Le pilote actif et compilé est src/vasio_driver.cpp (TimoxVasio, 256/256).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../asiosdk/common/asio.h"
#include "../asiosdk/host/asiodrivers.h"

// ============================================================================
// VASIO Driver - Implémentation ASIO SDK correcte
// ============================================================================

#define DRIVER_ID 1

struct DriverState {
    // Callbacks ASIO
    ASIOCallbacks callbacks;

    // Buffers
    ASIOBufferInfo* bufferInfos;
    long numChannels;
    long bufferSize;
    int currentBuffer;  // 0 ou 1 (ping-pong)

    // Info driver
    char driverName[256];
    int driverVersion;

    // État
    bool isRunning;
    ASIOSampleRate sampleRate;
};

static DriverState g_state = {0};

// ============================================================================
// Callbacks ASIO - DOIVENT être ultra-rapides (< 1ms)
// ============================================================================

void bufferSwitchCallback(long index, ASIOBool processNow) {
    // index = 0 ou 1 (double buffer)
    g_state.currentBuffer = index;

    // ⚠️ CRITICAL: Cette fonction DOIT retourner rapidement
    if (processNow) {
        // Traiter audio immédiatement
        for (long i = 0; i < g_state.numChannels; i++) {
            ASIOBufferInfo* info = &g_state.bufferInfos[i];
            if (info->isInput) {
                float* inputBuf = (float*)info->buffers[index];
                // Traiter input
            } else {
                float* outputBuf = (float*)info->buffers[index];
                // Remplir output
                memset(outputBuf, 0, g_state.bufferSize * sizeof(float));
            }
        }
    }

    // Signal que output est prêt
    g_state.callbacks.asioMessage(kAsioSelectorSupported, kAsioEngineVersion, NULL, NULL);
}

long ASIOMessageCallback(long selector, long value, void* message, double* opt) {
    // Message callback - peut être appelé à tout moment
    switch (selector) {
        case kAsioSelectorSupported:
            if (value == kAsioEngineVersion) {
                return 1;
            }
            break;
        case kAsioEngineVersion:
            // Retourner version du moteur
            return 2400;  // ASIO 2.4
        case kAsioResetRequest:
            // Demande de reset
            return 1;  // Accepter
        case kAsioBufferSizeChange:
            // Changement taille buffer
            return 0;  // Pas supporté
        case kAsioResyncRequest:
            // Resync demandé
            return 0;
    }
    return 0;
}

// ============================================================================
// Interface IASIO - Implémentation des méthodes requises
// ============================================================================

// Initialisation
ASIOBool ASIOInit(void* sysHandle) {
    memset(&g_state, 0, sizeof(g_state));
    strcpy(g_state.driverName, "VASIO1");
    g_state.driverVersion = 100;
    g_state.sampleRate = 44100.0;
    return ASIOTrue;
}

void ASIOGetDriverName(char* name) {
    strcpy(name, g_state.driverName);
}

long ASIOGetDriverVersion() {
    return g_state.driverVersion;
}

// Capabilities
ASIOError ASIOGetChannels(long* numInputChannels, long* numOutputChannels) {
    // VASIO1 a 6 entrées et 6 sorties
    *numInputChannels = 6;
    *numOutputChannels = 6;
    return ASE_OK;
}

ASIOError ASIOGetBufferSize(long* minSize, long* maxSize,
                            long* preferredSize, long* granularity) {
    *minSize = 64;
    *maxSize = 8192;
    *preferredSize = 256;
    *granularity = 1;
    return ASE_OK;
}

ASIOError ASIOCanSampleRate(ASIOSampleRate sampleRate) {
    // Supporter fréquences communes
    if (sampleRate == 44100.0 || sampleRate == 48000.0 ||
        sampleRate == 88200.0 || sampleRate == 96000.0) {
        return ASE_OK;
    }
    return ASE_NotPresent;
}

ASIOError ASIOGetSampleRate(ASIOSampleRate* sampleRate) {
    *sampleRate = g_state.sampleRate;
    return ASE_OK;
}

ASIOError ASIOSetSampleRate(ASIOSampleRate sampleRate) {
    if (ASIOCanSampleRate(sampleRate) == ASE_OK) {
        g_state.sampleRate = sampleRate;
        return ASE_OK;
    }
    return ASE_NotPresent;
}

// Buffer management
ASIOError ASIOCreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                            long bufferSize, ASIOCallbacks* callbacks) {
    g_state.bufferInfos = bufferInfos;
    g_state.numChannels = numChannels;
    g_state.bufferSize = bufferSize;
    g_state.callbacks = *callbacks;

    // Allouer buffers (double buffering)
    for (long i = 0; i < numChannels; i++) {
        bufferInfos[i].buffers[0] = malloc(bufferSize * sizeof(float));
        bufferInfos[i].buffers[1] = malloc(bufferSize * sizeof(float));

        if (!bufferInfos[i].buffers[0] || !bufferInfos[i].buffers[1]) {
            return ASE_NoMemory;
        }

        // Initialiser à zéro
        memset(bufferInfos[i].buffers[0], 0, bufferSize * sizeof(float));
        memset(bufferInfos[i].buffers[1], 0, bufferSize * sizeof(float));
    }

    return ASE_OK;
}

ASIOError ASIODisposeBuffers(void) {
    for (long i = 0; i < g_state.numChannels; i++) {
        if (g_state.bufferInfos[i].buffers[0]) {
            free(g_state.bufferInfos[i].buffers[0]);
        }
        if (g_state.bufferInfos[i].buffers[1]) {
            free(g_state.bufferInfos[i].buffers[1]);
        }
    }
    return ASE_OK;
}

// Contrôle
ASIOError ASIOStart(void) {
    g_state.isRunning = true;
    // Simuler démarrage (dans une vraie impl, start kernel timer)
    return ASE_OK;
}

ASIOError ASIOStop(void) {
    g_state.isRunning = false;
    return ASE_OK;
}

ASIOError ASIOGetLatencies(long* inputLatency, long* outputLatency) {
    // Latence = 1 buffer (simplifié)
    *inputLatency = g_state.bufferSize;
    *outputLatency = g_state.bufferSize;
    return ASE_OK;
}

ASIOError ASIOOutputReady(void) {
    // Signal que l'output est prêt pour ce buffer
    // Dans une vraie impl, notifier le kernel
    return ASE_OK;
}

// Info canaux
ASIOError ASIOGetChannelInfo(ASIOChannelInfo* info) {
    if (info->channel < 0 || info->channel >= g_state.numChannels) {
        return ASE_InvalidParameter;
    }

    info->isActive = ASIOTrue;
    info->type = ASIOSTFloat32LSB;  // 32-bit float little-endian

    char name[32];
    sprintf(name, "VASIO%d:%ld", DRIVER_ID, info->channel + 1);
    strcpy(info->name, name);

    return ASE_OK;
}

// Contrôle avancé
ASIOError ASIOControlPanel(void) {
    // Pas de panel pour drivers virtuels
    return ASE_NotPresent;
}

ASIOError ASIOFuture(long selector, void* opt) {
    // Extensibilité future
    return ASE_NotPresent;
}

ASIOError ASIODispose(void) {
    if (g_state.isRunning) {
        ASIOStop();
    }
    return ASE_OK;
}

// ============================================================================
// Export DLL - Fonction d'entrée pour Windows
// ============================================================================

extern "C" {
    __declspec(dllexport) ASIODriver* (*asioCreateDriver)(void) = 0;

    BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
        switch (reason) {
            case DLL_PROCESS_ATTACH:
                asioCreateDriver = []() -> ASIODriver* {
                    // Créer instance du driver
                    ASIOInit(NULL);
                    // Retourner pointeur IASIO
                    return (ASIODriver*)1;  // Placeholder
                };
                break;
            case DLL_PROCESS_DETACH:
                break;
        }
        return TRUE;
    }
}

// ============================================================================
// Registration Windows - Enregistrement dans le registre
// ============================================================================

void RegisterVASIODriver(const char* driverName, const char* dllPath) {
    HKEY hKey;
    DWORD disposition;
    char subKey[256];
    char clsid[64] = "{12345678-1234-1234-1234-123456789ABC}";

    // Créer clé ASIO
    sprintf(subKey, "SOFTWARE\\Steinberg\\ASIO\\%s", driverName);
    RegCreateKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, NULL,
                   REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disposition);

    // Ajouter CLSID
    RegSetValueExA(hKey, "CLSID", 0, REG_SZ, (LPBYTE)clsid, strlen(clsid) + 1);
    RegCloseKey(hKey);

    // Créer clé CLSID
    sprintf(subKey, "SOFTWARE\\Classes\\CLSID\\%s", clsid);
    RegCreateKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, NULL,
                   REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disposition);
    RegCloseKey(hKey);

    // Créer clé InprocServer32
    sprintf(subKey, "SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    RegCreateKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, NULL,
                   REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disposition);

    RegSetValueExA(hKey, NULL, 0, REG_SZ, (LPBYTE)dllPath, strlen(dllPath) + 1);
    RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ, (LPBYTE)"Apartment", 9);
    RegCloseKey(hKey);
}

void UnregisterVASIODriver(const char* driverName) {
    char subKey[256];

    // Supprimer clé ASIO
    sprintf(subKey, "SOFTWARE\\Steinberg\\ASIO\\%s", driverName);
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, subKey);
}
