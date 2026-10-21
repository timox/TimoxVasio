# Première qualification réelle ASIO sans carte physique

Essai du 22 octobre 2026 exécuté par l'agent principal. **Succès court sur le trajet app→app ; qualification longue et WASAPI encore ouvertes.**

## Trajet et procédure

`AsioSignal-send.exe → TimoxVasio.dll → transport existant → moteur / horloge logicielle → transport existant → TimoxVasio.dll → AsioSignal-receive.exe`.

Les deux hôtes utilisent `DllGetClassObject` et l'interface ASIO réelle, avec la DLL canonique chargée par chemin absolu. Aucun transport simulé, aucune inscription registre. Ils utilisent des noms différents afin que la résolution persistante par nom d'application reste non ambiguë. La route est créée depuis les ports réellement découverts par l'API, à gain nul en dB, sans mute.

Control installé et son moteur ont été temporairement interrompus conformément à l'autorisation du mainteneur. Le moteur de test a reçu un `LOCALAPPDATA` privé sous `build_wasapi_e2e`, avec configuration logicielle 48000 Hz/2048 frames et aucune route initiale. Les applications utilisateur sont restées ouvertes ; trois clients préexistants étaient visibles mais n'avaient aucune route dans cette configuration de test. Ce n'est pas un environnement de mesure CPU isolé.

Commande de qualification : `tests/invoke-asio-engine-signal-probe.ps1 -ApiPort 52525 -DriverPath <DLL canonique> -ProbePath <AsioEngineSignalProbe.exe canonique> -Seconds 5 -AllowExistingClients -ResultsRoot <dossier de session>`.

Les processus attendent la configuration des routes sur stdin avant Start. Deux secondes de chauffe précèdent l'analyse. Le récepteur mesure cinq secondes ; l'émetteur continue deux secondes de plus pour éviter un arrêt prématuré. L'orchestrateur restaure la configuration de test seulement si la révision lui appartient encore et ne termine que ses propres processus sondes.

## Résultat

Script et sondes : code 0. `passed: true`, restauration de la route temporaire confirmée.

| Mesure récepteur | Valeur |
|---|---:|
| Frames analysées | 240000 |
| Callbacks, chauffe comprise | 165 |
| Amplitude attendue de chaque ton | 0,0005 |
| Amplitude 997 Hz | 0,000500000000043 |
| Amplitude 1709 Hz | 0,000500000000080 |
| RMS | 0,000500000000061 |
| SNR par projection des deux tons | 129,55658 dB |
| Échantillons non finis | 0 |
| Demande reset/resync | aucune |
| Échantillons exactement nuls | 5 |

Les cinq zéros sont un décompte de valeurs, pas un compteur d'underruns : le signal à deux tons traverse zéro. Le SNR ne mesure pas la latence. Les statistiques de réception ne sont pas calculées par l'émetteur.

Le moteur de test a ensuite été terminé et Control installé relancé. Vérification indépendante après restauration : API 1.0, état `running`, 48000 Hz/2048 frames, 22 routes, trois clients, buffer health `healthy`, aucune erreur moteur. Le SHA-256 du fichier utilisateur `configuration.json` est identique avant/après. Aucun binaire installé ni pilote enregistré n'a été remplacé.

## Provenance

- DLL : `build_driver_110/Release/TimoxVasio.dll`, UTC `2026-10-21T06:03:48.2195236Z`, SHA-256 `F4FC8AA0EDB929B74DB2459D6B69652955A1BE9FB21370C6A109DDB96983A0B6`.
- Sonde : `build_codex_110/Release/AsioEngineSignalProbe.exe`, UTC `2026-10-21T22:53:06.6312147Z`, SHA-256 `4B483C169EDAA94EB44DA723C7EC1784B79D8DEF3B21E953371F65B262D36F42`.
- Moteur : `build_codex_110/Release/TimoxVirtualAsioEngine.exe`, SHA-256 `7EA53418E5337A9F75B7DD95A04A5481C0D992E82DB588E1D1965199C26F2175` (build d'intégration antérieur à la fusion des métadonnées de version stable ; pas un paquet de release).
- Session locale : `build_wasapi_e2e/session-073d0f8ce1b24661a3ed8faeb470e99f`, rapport `asio-engine-0f3b96c4e47241e8aa4ccc359a92b2d4/report.json`. Les états utilisateur détaillés restent locaux et ne sont pas publiés.

## Suite requise

Autres fréquences/quanta, dix minutes stables, pertes et reprises séparées, trajet véritable ASIO→WASAPI puis loopback→ASIO, câble virtuel vers application Windows, latence par corrélation et distributions de temps sous charge. Deux hôtes de qualification réels ne prouvent pas la compatibilité de toutes les stations audio du marché.
