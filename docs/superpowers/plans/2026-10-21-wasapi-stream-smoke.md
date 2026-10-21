# Première sonde WASAPI réelle

Essai du 21 octobre 2026, lancé par l'agent principal. Ce rapport prouve un streaming court ; pas une qualification de production.

## Montage

`SoftwareClockHost 48000/256 → FIFO rendu → WasapiBridge → rendu partagé SSL12 → loopback du même endpoint → WasapiBridge → FIFO capture → lecture par horloge logicielle`.

Signal 997 Hz, amplitude 0,001 (-60 dBFS), environ quatre secondes. Les données capture ne sont pas enregistrées ; seuls des agrégats sont conservés. Pas de route de retour capture→rendu, pas de changement du périphérique par défaut, pas de remplacement/arrêt du moteur installé.

Commande : `build_codex_110/Release/WasapiBridgeProbe.exe --run-default-render-loopback`.

Endpoint résolu : `Output 3/4 (SSL 12 USB Audio Device)` ; ID `{0.0.0.00000000}.{c2461d21-c654-47bb-8360-90350a202f91}`. Format natif observé : 48000 Hz, 2 canaux. La capture est un loopback de sortie, pas l'entrée microphone auparavant refusée.

## Résultat

Code retour **0**.

| Mesure | Valeur |
|---|---:|
| Amplitude estimée à 997 Hz | 0,000465756 |
| RMS capture | 0,000369907 |
| Trames natives capturées | 194880 |
| Trames natives rendues | 193056 |
| Trames lues par le graphe | 191744 |
| Trames acceptées par le FIFO rendu | 193792 |
| Trames capture rejetées | 0 |
| Trames manquantes fournies au SRC rendu | 0 |
| HRESULT capture/rendu | S_OK / S_OK |

Les compteurs portent sur des points différents, avec préremplissage, démarrage et arrêt différents ; leur différence n'est pas une mesure de pertes. L'amplitude dépend aussi des volumes/traitements du système et du déphasage pendant la fenêtre. La sonde détecte une composante au signal attendu ; elle ne mesure ni fidélité bit à bit ni délai de bout en bout, et ne prétend pas exclure toute autre source au même ton.

## Provenance et limites

Binaire : `build_codex_110/Release/WasapiBridgeProbe.exe`, 101376 octets, UTC `2026-10-21T21:56:52.7526111Z`, SHA-256 `65714C94C62DF23BD582959D87C6CF9615A73ECC5DBC49E2E086CFBAF3F837BF`.

Sources : `tests/wasapi_bridge_probe.cpp`, worker/backend WASAPI, conversion PCM/SRC et horloge logicielle de cette branche non encore commitée. Cette exécution n'utilise pas le contrôleur/API/GUI de production, pas un client ASIO, ni le futur pilote Windows virtuel. Elle ne remplace pas les essais 10 minutes, la comparaison CPU, les pertes/reconnexions et les trajets interapplications du plan.
