# Préparer et vérifier la chaîne de compilation du pilote natif

Les scripts de ce dossier préparent une **compilation de référence Microsoft SysVAD**, avant adaptation à TimoxWindowsAudio. Ils ne créent pas encore notre pilote, n'installent aucun endpoint et ne modifient ni TESTSIGNING, ni Secure Boot, ni les certificats.

## Versions et choix de chaîne

`dependencies.json` épingle le dépôt Microsoft au commit `bb68ed5e770bce04dc5395ec9258dafab034184d`, les trois packages x64 WDK/SDK à `10.0.28000.2526`, et NuGet à `7.9.0.83` avec son SHA-256. Les versions WDK/SDK sont celles du `packages.config` de cette révision officielle. Le répertoire interne des kits est `10.0.28000.0`, distinct du numéro de package.

Le poste possède Visual Studio Community 2026 18.11 (MSVC 14.51) et Build Tools 2022 17.14 (MSVC 14.44). La route retenue utilise VS2026 et le WDK actuel en packages locaux. Microsoft recommande WDK 28000.2526 pour VS2026 ; VS2022 nécessiterait l'ensemble 26100.6584 et une validation séparée. EWDK est une alternative autonome contenant compilateur/SDK/WDK dans une ISO, mais n'est pas requis pour ce poste. [Installation WDK](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk), [WDK NuGet](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget).

Les packages NuGet ne suffisent pas sans l'intégration WDK de Visual Studio. Le **composant installable** est `Component.Microsoft.Windows.DriverKit` dans le catalogue officiel VS2026 du poste ; il apporte notamment `Microsoft.VisualStudio.WindowsDriverKit.Build` et les bibliothèques Spectre. Ne pas le confondre avec `Microsoft.Windows.DriverKit`, ID du package VSIX recherché par le [script officiel de détection Microsoft](https://github.com/microsoft/Windows-driver-samples/blob/bb68ed5e770bce04dc5395ec9258dafab034184d/BuildEnvironment.ps1). Le composant Spectre x64/x86 est `Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre`, documenté dans les [identifiants de composants Visual Studio](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-community?view=visualstudio). Aucun script de ce dossier n'installe ces composants globalement ni ne désactive les mitigations pour réussir une compilation.

## Acquisition puis compilation

Depuis la racine Git :

```powershell
# Inventaire seul : code 2 si un élément requis manque ; aucune mutation.
pwsh -NoProfile -File native-windows-audio/scripts/check-build-prerequisites.ps1 -Json

# Acquisition officielle dans build_native_audio_wdk, puis build de référence.
pwsh -NoProfile -File native-windows-audio/scripts/build-sysvad-probe.ps1 -Acquire

# Rebuild sans acquisition, une fois les prérequis présents.
pwsh -NoProfile -File native-windows-audio/scripts/build-sysvad-probe.ps1
```

L'acquisition vérifie la signature Authenticode Microsoft de NuGet et son hash avant exécution. Le clone sparse épingle le commit exact ; si un checkout existant diffère, le script s'arrête sans le réinitialiser. Il restaure uniquement les packages SDK commun, SDK x64 et WDK x64 depuis `api.nuget.org`, avec `-DirectDownload`. Les fichiers téléchargés/buildés restent sous `/build_native_audio_wdk/`, ignoré par Git. La première acquisition manuelle de cette session a également alimenté le cache NuGet utilisateur standard ; il ne s'agit pas d'une installation du WDK système.

Le préflight vérifie les imports, headers noyau, portcls.lib, InfVerif/Inf2Cat/StampInf, compilateur, bibliothèque Spectre, composant WDK VS et commit SysVAD. Il ne confond pas leur présence avec une preuve de compilation. Le build lance EndpointsCommon puis TabletAudioSample en Release/x64, un processus MSBuild **hôte amd64** à la fois, avec `SignMode=Off`. Le chemin StampInf x64 est explicite. Les journaux sont dans le dossier de build. Aucun target de déploiement ou installation n'est appelé. Après succès, le script affiche chemin, date, taille, état de signature et SHA-256 du `.sys` produit.

La licence du code SysVAD récupéré est [MS-PL](https://github.com/microsoft/Windows-driver-samples/blob/bb68ed5e770bce04dc5395ec9258dafab034184d/LICENSE). Lors de l'adaptation dans le dépôt public, conserver la licence complète et les notices de chaque fichier réutilisé ; garder un historique des modifications. À ce stade, les sources upstream sont seulement dans le checkout ignoré.

## Résultats constatés le 22 octobre 2026

- NuGet officiel : signature Microsoft `Valid`, SHA-256 `992D70CAC5B06C38EFEC91806CABA64CDCC07E6D963A0959DBBBAF264D33B800` ; URL versionnée vérifiée HTTP 200.
- Checkout SysVAD au commit prévu et restauration des trois packages : succès.
- Première compilation EndpointsCommon : **MSB8020**, toolset `WindowsKernelModeDriver10.0` introuvable, avant compilation du code. L'intégration WDK VS était absente ; bibliothèque Spectre x64 également absente à l'inventaire.
- Une première invocation de VSInstaller avec l'ID VSIX `Microsoft.Windows.DriverKit` et l'ID Spectre a retourné zéro mais n'a ajouté que Spectre. L'inventaire a détecté le manque persistant ; l'ID installable correct a été confirmé dans le catalogue local : `Component.Microsoft.Windows.DriverKit`.
- Après installation du composant correct, le préflight est entièrement vert. Premier build : EndpointsCommon réussi, puis recherche StampInf x86 impossible. Après indication du chemin StampInf x64 : `.sys` produit, mais validations INF/API en échec car l'hôte MSBuild était encore x86. Ce binaire intermédiaire n'a pas été considéré comme un build réussi.
- Correction finale : MSBuild `Current/Bin/amd64/MSBuild.exe`, `PreferredToolArchitecture=x64`, StampInf x64 explicite. **Build final terminé avec code 0**, InfVerif exécuté et ApiValidator concluant `All binaries are Universal`. Aucun contrôle de validation ou mitigation n'a été désactivé ; la signature est volontairement désactivée pour cette compilation de référence.

Provenance du `.sys` de référence :

| Champ | Valeur |
|---|---|
| Chemin relatif au dépôt | `build_native_audio_wdk/Windows-driver-samples/audio/sysvad/TabletAudioSample/x64/Release/TabletAudioSample.sys` |
| Taille | 240128 octets |
| Dernière écriture UTC | `2026-10-22T05:23:18.8166723Z` |
| SHA-256 | `AE019AC99F178AF1761806745ECE122E61DF1C099039F4BFEBE61046E9BED111` |
| Signature | `NotSigned` |
| Sources | Checkout Microsoft épinglé ; `git status --short` vide après compilation |

Le timestamp précède la dernière validation : le link avait réussi pendant l'essai intermédiaire ; le build final a réutilisé ce binaire et a terminé les validations. Le contenu n'est pas assimilé à une distribution signée. Journaux finaux : `build_native_audio_wdk/EndpointsCommon-build.log` et `TabletAudioSample-build.log`.

Même une compilation SysVAD réussie ne sera pas la livraison TimoxWindowsAudio : adaptation des endpoints, transport privé, tests noyau, sécurité, signature et installation restent des étapes distinctes.
