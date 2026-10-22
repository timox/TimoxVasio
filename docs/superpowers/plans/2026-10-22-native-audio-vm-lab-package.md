# Paquet laboratoire VM et devnode natif — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fournir un paquet laboratoire traçable et un petit utilitaire SetupAPI propre pour créer et retirer exclusivement notre périphérique racine dans une VM Windows dédiée.

**Architecture:** Construction et préparation du paquet restent séparées de son activation. L'utilitaire ne fait que gérer un devnode précis ; PnPUtil garde la responsabilité du staging et de l'installation du paquet. Toute mutation nécessite une commande explicite dans la cible autorisée, avec reçu durable, vérifications d'identité et rollback ciblé.

**Tech Stack:** C++17 x64, SetupAPI, CfgMgr32 si nécessaire, PowerShell, CMake autonome, SDK/WDK Microsoft existants, SignTool et Inf2Cat.

**Spec:** `docs/superpowers/specs/2026-10-22-native-windows-audio-driver.md`, `native-windows-audio/driver/README.md`, `tests/native_audio_lab_probe.md`, `docs/native-audio-session-qualification.md`.

## Global Constraints

- Aucun chargement, certificat, TESTSIGNING, changement Secure Boot ou installation sur l'hôte de développement dans ce lot.
- Aucune connexion à la VM avant le signal utilisateur ; le présent plan n'exécute aucune mutation.
- `TimoxVasio.dll`, le moteur permanent et Electron restent autonomes et hors paquet laboratoire.
- Cible x64, Windows build 22621 minimum suivant l'INF actuel ; enregistrer la version réelle de la VM avant de choisir les options Inf2Cat.
- Hardware ID fixe `Root\TimoxWindowsAudio`, classe MEDIA `{4d36e96c-e325-11ce-bfc1-08002be10318}`, service `TimoxWindowsAudio` ; aucun argument permettant de les remplacer.
- Un seul adaptateur. Refuser les doublons présents **ou non présents**, ne pas adopter automatiquement un périphérique préexistant.
- DevGen n'est pas redistribuable : ne pas inclure DevGen/DevCon dans le paquet ; notre exécutable utilise les API système.
- Avant toute compilation : `git rev-parse --show-toplevel` puis `git status --short`. Builds isolés coordonnés avec le parent, aucun commit non coordonné.

## Review Focus

- Crash entre enregistrement du devnode et écriture du résultat : conserver une intention durable permettant une reprise ciblée (lot 1).
- Reçu corrompu, copié d'une autre machine ou fichier reparse : refuser toute suppression avant validation (lots 1–2).
- Doublon fantôme ou création concurrente : refuser une adoption implicite et ne jamais supprimer l'instance concurrente (lots 1–2).
- Signature ajoutée après génération du catalogue : invalider le paquet plutôt que publier des hashes incohérents (lot 3).
- Retrait nécessitant redémarrage ou probe non exécutable sur VM vierge : conserver un état incomplet explicite, pas un faux succès (lots 2–4).

## Fichiers et frontières

Tous les fichiers suivants sont **nouveaux**, sauf les sources de paquet listées au lot 3 :

| Chemin | Responsabilité |
|---|---|
| `native-windows-audio/lab-device/CMakeLists.txt` | Projet autonome outil/tests ; aucune installation CTest |
| `native-windows-audio/lab-device/device_transaction.h/.cpp` | Politique, validation du reçu, orchestration via interface injectable |
| `native-windows-audio/lab-device/device_win32.h/.cpp` | SetupAPI, identité machine, reçu protégé et verrou interprocessus |
| `native-windows-audio/lab-device/main.cpp` | CLI, JSON résultat, aucun auto-UAC |
| `native-windows-audio/lab-device/tests/device_transaction_tests.cpp` | Faux Windows/stockage et fautes à chaque étape |
| `native-windows-audio/scripts/prepare-vm-lab-package.ps1` | Copie vérifiée et manifeste non signé ; aucune signature/installation |
| `native-windows-audio/scripts/test-vm-lab-package.ps1` | Contrôles de cohérence du paquet, sans installation |
| `native-windows-audio/lab-device/README.md` | Contrat CLI, récupération et limites |
| `docs/native-audio-vm-lab-runbook.md` | Signature, activation VM, probes, preuves, rollback |

## Lot 1 — Transaction testable et reçu

**Interfaces :** `enum class Action { Inspect, Create, Remove };`, `Request { Action action; bool apply; }`, `Result { status, win32Error, instanceId, rebootRequired, recoveryRequired }`. `Execute(const Request&, DevicePlatform&) -> Result` n'expose ni hardware ID arbitraire, ni INF arbitraire. `DevicePlatform` fournit inventaire, verrou, création/enregistrement, retrait, identité machine et lecture/écriture durable du reçu. Les retours conservent l'erreur Windows originale ; pas de booléen qui confond absence et erreur.

- [ ] Écrire les tests RED : help/dry-run ne mutent rien ; inventaire échoué interdit Create ; un hardware ID exact dans un MULTI_SZ suffit au refus doublon, comparaison sans casse ; préfixes ressemblants ne correspondent pas ; fantôme inclus ; données tronquées/type incorrect/tailles excessives refusées.
- [ ] Ajouter les scénarios RED de transaction : échec stockage avant enregistrement interdit enregistrement ; échec après enregistrement conserve intention et instance ; retrait échoué conserve reçu ; retrait absent déjà réalisé est idempotent ; reboot requis n'est pas annoncé comme disparition vérifiée.
- [ ] Implémenter le minimum puis GREEN. Reçu schema 1 : identifiant aléatoire d'opération, identité machine locale, hardware ID et classe constants, instance complète, étape `prepared/registered/removal-pending/removed`. Aucune clé privée ni secret dans ce reçu.
- [ ] Choisir dès la préparation une instance explicite de forme `ROOT\TimoxWindowsAudioLab\<GUID>`, créée avec SetupDiCreateDeviceInfoW sans `DICD_GENERATE_ID`. Vérifier cette capacité dans la VM avant de déclarer le wrapper qualifié ; en cas de refus Windows, ne pas basculer silencieusement vers un nom inconnu avant enregistrement.
- [ ] Écrire durablement `prepared` avant `DIF_REGISTERDEVICE`. Après erreur ambiguë de registration, rouvrir uniquement cette instance et constater sa présence/identité ; ne pas prétendre qu'un appel échoué implique zéro effet.
- [ ] Tests RED/GREEN : crash simulé à chaque frontière, reçu d'une autre machine, instance hors préfixe, GUID invalide, classe/hardware ID différents, deux opérations concurrentes. Une reprise n'adopte jamais un autre devnode trouvé par hardware ID.
- [ ] Revue indépendante puis commit du lot testable.

## Lot 2 — Wrapper SetupAPI et CLI bornée

**CLI :** `TimoxNativeLabDevice.exe` et `--help` affichent l'aide ; `inspect` lit l'état ; `create` et `remove` sont dry-run par défaut ; seules `create --apply` et `remove --apply` mutent. Aucun `--force`, hardware ID, instance ou chemin de reçu fourni par l'appelant. Pas de lancement UAC automatique. Code 0 succès, 2 argument/invariant refusé, 3 erreur Windows/rollback requis, 3010 redémarrage requis ; JSON indique les distinctions.

- [ ] Tests RED des arguments inconnus/contradictoires et de l'absence d'élévation : aucune opération mutante appelée. Tester les retours Windows associés sans exécuter SetupAPI mutante dans CTest.
- [ ] Implémenter un dossier de reçu fixe sous ProgramData `TimoxVasio\NativeAudioLab`, accessible SYSTEM/Administrateurs seulement. Refuser les composants reparse et les fichiers existants non sûrs ; utiliser création exclusive, remplacement atomique et flush. Protéger l'opération par verrou interprocessus conservé jusqu'à écriture du résultat ; un verrou précréé non fiable provoque un refus, pas une exécution non sérialisée. L'identité machine n'est pas une frontière contre un administrateur hostile ; le reçu prévient surtout les erreurs de cible.
- [ ] Inventorier les devices sans `DIGCF_PRESENT`, avec tailles plafonnées et lecture sûre de `SPDRP_HARDWAREID` REG_MULTI_SZ ; aucune simple comparaison de sous-chaîne. Sous verrou, refaire l'inventaire immédiatement avant Create.
- [ ] Créer un HDEVINFO de classe MEDIA ; `SetupDiCreateDeviceInfoW`, `SetupDiSetDeviceRegistryPropertyW(SPDRP_HARDWAREID)` avec double NUL, reçu `prepared`, puis `SetupDiCallClassInstaller(DIF_REGISTERDEVICE)`. Libérer les HDEVINFO sur tous les chemins. Enregistrer l'instance effectivement retournée et vérifier son égalité à l'intention.
- [ ] Après Create, refaire l'inventaire pour détecter une création externe concurrente : si doublon, rollback seulement de notre nouvelle instance enregistrée ; conserver une erreur si ce rollback échoue. La sérialisation de notre outil ne verrouille pas tous les installateurs Windows.
- [ ] Remove lit exclusivement le reçu protégé, ouvre l'instance enregistrée, revérifie classe/hardware ID/identité et demande `DIF_REMOVE` avec `SP_REMOVEDEVICE_PARAMS` scope `DI_REMOVEDEVICE_GLOBAL`. Ne pas appeler directement `SetupDiRemoveDevice`, réservé aux class installers. Lire les indicateurs `DI_NEEDREBOOT/DI_NEEDRESTART` et vérifier l'absence avant état final.
- [ ] Fixer la compilation x64 `/W4 /WX`, runtime statique `/MT` Release de l'outil autonome. Lier Setupapi ; n'ajouter Cfgmgr32/Advapi32 qu'aux usages réels. Ne pas modifier le runtime global du dépôt.
- [ ] Commandes de validation locale sans mutation : `cmake -S native-windows-audio/lab-device -B build_native_lab_device`, `cmake --build build_native_lab_device --config Release`, `ctest --test-dir build_native_lab_device -C Release --output-on-failure`. CTest ne contient que faux Windows et aide, jamais `--apply`.
- [ ] Revue des droits, reprise après crash et cleanup ; commit. Qualification SetupAPI réelle reportée au lot 4.

## Lot 3 — Paquet et manifeste, sans identité de signature créée ici

**Sources du paquet :** `build_native_windows_audio/x64/Release/TimoxWindowsAudio/{TimoxWindowsAudio.sys,TimoxWindowsAudio.inf,timoxwindowsaudio.cat}`, PDB au répertoire parent ; exclusivement `build_native_lab_protocol/Release/NativeAudioDriverProbe.exe` et `build_native_lab_transport/Release/NativeAudioLabProbe.exe` pour les probes ; notices `native-windows-audio/driver/THIRD_PARTY_NOTICES.md` et `upstream/LICENSE`. Ajouter `build_native_lab_device/Release/TimoxNativeLabDevice.exe` du lot 2. Les deux répertoires de probes laboratoire sont de nouvelles sorties isolées ; ne pas sélectionner les anciens builds `/MD` comme repli.

- [ ] Tests RED du préparateur sur mini-fixtures : source absente, copie différente, destination existante, reparse, manifeste incomplet et mutation d'un octet après copie. Le script n'utilise pas de fixture noyau exécutable et ne lance pas de signature.
- [ ] Implémenter `prepare-vm-lab-package.ps1 -OutputDirectory <nouveau dossier>` : sources fixes ci-dessus, refus d'écraser une sortie, contrôle des copies SYS/INF entre répertoire de build et package, licence, manifeste schema 1 avec chemins relatifs, taille, SHA-256, date UTC, commit et état Git, version/architecture, outils utilisés. Ne pas attribuer un binaire à HEAD sans preuve de build correspondante ; rebuild coordonné si la provenance manque.
- [ ] Distribuer `driver/`, `symbols/`, `probes/`, `tools/TimoxNativeLabDevice.exe`, `licenses/`, `docs/`, `manifest-unsigned.json`. Ne pas mélanger les outils WDK au dossier driver et ne pas recopier les clés/caches/signing stores.
- [ ] Compiler exclusivement les variantes laboratoire `/MT` ci-dessous, puis vérifier les imports PE : aucune dépendance runtime MSVC dynamique, DLL système Windows admises. Ces commandes utilisent la cible Windows de `native-windows-audio/CMakeLists.txt` et l'option existante du CMake racine ; aucun SDK ASIO requis. Ne pas remplacer les binaires canoniques.

```powershell
cmake -S native-windows-audio -B build_native_lab_protocol -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build_native_lab_protocol --config Release --target NativeAudioDriverProbe
cmake -S . -B build_native_lab_transport -A x64 -DVASIO_BUILD_NATIVE_TRANSPORT_TESTS_ONLY=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build_native_lab_transport --config Release --target NativeAudioLabProbe
```
- [ ] Documenter la phase signature **séparée, uniquement dans l'environnement autorisé** : certificat CodeSigning dédié non exportable ; signer SYS avec SHA-256 ; générer CAT à partir du SYS déjà signé avec Inf2Cat et OS cible exact ; signer CAT ; vérifier signature embarquée, catalogue et appartenance INF/SYS ; produire `manifest-signed.json`. Conserver le manifeste non signé pour corrélation, ne pas comparer les hashes SYS avant/après signature comme s'ils devaient rester identiques.
- [ ] `test-vm-lab-package.ps1 -PackageDirectory <dossier>` vérifie fichiers exacts, hashes et état attendu unsigned/signed ; refuse incohérence sans tenter de réparer/signature automatique. En phase signée, SignTool `/pa /v /c <cat> <inf|sys>` vérifie l'appartenance sous confiance laboratoire appropriée ; enregistrer aussi les diagnostics `/kp`, sans assimiler confiance de test à signature de production Microsoft.
- [ ] Tests GREEN des mutations après signature/manifeste, faux certificat ou signature absente via runner injectable. Revue puis commit des scripts/runbook ; aucun fichier privé ou binaire publié dans Git.

## Lot 4 — Activation et qualification dans la VM après signal

- [ ] Recevoir l'identité de la VM/accès et le signal de disponibilité ; snapshot hors tension ou cohérent avant mutation. Noter OS/build, Secure Boot, HVCI, boot policy, hash paquet reçu, chemin de snapshot et possibilité console de récupération. Pas de détection heuristique « c'est sûrement une VM » comme autorisation.
- [ ] Préparer signature/outils officiels dans l'environnement laboratoire choisi ; la clé reste là. Installer seulement le certificat public de test dans les magasins requis de la VM. TESTSIGNING nécessite redémarrage ; si Secure Boot bloque, traiter explicitement la configuration de la VM, jamais celle de l'hôte. Garder HVCI observé, ne pas le désactiver pour cacher un échec de pilote.
- [ ] Vérifier hashes/signatures puis `TimoxNativeLabDevice inspect`, `create` dry-run, `create --apply` une seule fois. Enregistrer le reçu et `pnputil /add-driver "driver\TimoxWindowsAudio.inf" /install` avec sortie/nom publié `oemNN.inf`. PnPUtil ne crée pas le devnode ; un échec de staging laisse une instance à retirer explicitement.
- [ ] Vérifier état PnP sans erreur et endpoints exacts, puis `NativeAudioDriverProbe --exercise-control` élevé. Vérifier refus d'un compte standard sur le contrôle privé ; le contrôle public audio a un contrat différent.
- [ ] `NativeAudioLabProbe --native --seconds 10`, puis essai WASAPI duplex explicitement routé `--loopback-audio` et preuve des motifs gauche/droite. Conserver les logs ; `physicalQualified:false` n'est pas remplacé par true parce que le transport s'ouvre.
- [ ] Tests retrait/crash/reprise, deuxième create refusé, redémarrage, reçu restant après interruption simulée. Driver Verifier limité au SYS Timox dans un passage séparé avec snapshot et récupération ; pas de qualification de latence physique à partir d'une VM KVM.
- [ ] Rollback : arrêter les probes, `remove` dry-run puis `remove --apply`, vérifier absence, `pnputil /delete-driver oemNN.inf /uninstall` sur le nom publié enregistré seulement. Aucun wildcard, `/force` ou reboot automatique. Sauver setupapi.dev.log/CodeIntegrity/dumps avant restauration snapshot ; si retour manuel requis, retirer seulement le certificat exact du laboratoire et restaurer les réglages VM enregistrés.
- [ ] Produire rapport distinguant compilation/tests simulés, installation, échange noyau, audio réel, lifecycle, isolation intersessions et performance ; les gates de production de N4 restent fermées tant que leurs preuves propres manquent.

## Sources Microsoft et points restant à confirmer sur cible

- [DevGen — non redistribuable](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devgen-command-syntax) ; [création ROOT distincte du staging](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-install).
- [SetupDiRegisterDeviceInfo et DIF_REGISTERDEVICE](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdiregisterdeviceinfo), [propriété hardware ID réservée aux devices ROOT](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdisetdeviceregistrypropertya).
- [Retrait via SetupAPI](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/using-setupapi-to-uninstall-devices-and-driver-packages), [scope de retrait](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/ns-setupapi-sp_removedevice_params), [PnPUtil](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).
- [Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat), [signature du paquet de test](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/test-signing-driver-packages), [vérification de signature](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/verifying-the-test-signature), [TESTSIGNING et HVCI](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).

Les choix de code et tests simulés ne nécessitent pas de décision supplémentaire. La version Windows exacte, les conditions de Secure Boot/HVCI, l'accès/snapshot VM et son signal de disponibilité sont les entrées requises avant le lot 4. La création d'une instance ROOT explicitement nommée, le chargement effectif et les chemins de retrait restent des preuves dynamiques à obtenir ; aucun test faux Windows ne les démontre.
