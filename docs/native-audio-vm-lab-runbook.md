# Paquet laboratoire Timox Windows Audio

Ce paquet contient un pilote noyau expérimental et des outils manuels pour une
VM Windows dédiée. **Le paquet initial est non signé et n'est pas installable
tel quel.** Aucun programme ne doit être lancé avec `--apply` sur l'hôte de
développement. Attendre le signal de disponibilité de la VM et conserver sa
console ainsi qu'un snapshot récupérable avant toute modification.

Le pilote ASIO `TimoxVasio.dll`, Electron et le moteur permanent ne font pas
partie de ce paquet. Ils ne sont ni remplacés ni enregistrés par cette procédure.

## Vérification du transfert

Comparer le SHA-256 de l'archive avec l'empreinte communiquée séparément. Extraire
dans un nouveau dossier local. Le manifeste `manifest-unsigned.json` décrit
chaque fichier, sa taille et son SHA-256 ; il n'est pas une signature de confiance.
La chaîne de transfert et l'origine du manifeste doivent donc être vérifiées.
Les preuves de build constituent une attestation de l'opérateur : la vérification
contrôle leur cohérence avec les quatre binaires, mais ne reconstitue pas à elle
seule la relation commit source → binaire. L'association GUID/âge entre le PDB
et le SYS n'est pas vérifiée par ce lot ; le PDB est seulement copié et hashé.
Ne pas transférer de clé privée SSH, PFX ou clé de signature avec le paquet.

La cible actuelle est Windows x64, build 22621 minimum. Relever la version exacte,
Secure Boot, HVCI, configuration de démarrage, identité VM et emplacement du
snapshot. Les tests de latence d'une VM KVM ne qualifient pas une latence physique.

## Signature de test : phase séparée

Cette phase ne fait pas partie du script de préparation unsigned. Elle nécessite
les outils officiels SDK/WDK dans l'environnement de signature autorisé. Une
identité de signature de laboratoire dédiée doit être créée/provisionnée là ;
sa clé reste non exportée et son empreinte est enregistrée. Seul le certificat
public est destiné à la VM. Ne pas installer cette confiance sur l'hôte par
commodité.

Ordre impératif : signer `driver/TimoxWindowsAudio.sys` en SHA-256, régénérer
`driver/TimoxWindowsAudio.cat` avec Inf2Cat pour l'OS cible exact, signer ce
catalogue en SHA-256. Ne pas réutiliser le catalogue unsigned après modification
du SYS. Vérifier signature embarquée et appartenance des fichiers au catalogue,
conserver les sorties SignTool et produire un nouveau manifeste signé avec
les nouveaux hashes. Les hashes unsigned et signed du SYS peuvent différer.

Commandes de vérification à adapter aux chemins des outils officiels :

```powershell
signtool verify /pa /v driver/TimoxWindowsAudio.cat
signtool verify /pa /v /c driver/TimoxWindowsAudio.cat driver/TimoxWindowsAudio.inf
signtool verify /pa /v /c driver/TimoxWindowsAudio.cat driver/TimoxWindowsAudio.sys
signtool verify /pa /v driver/TimoxWindowsAudio.sys
```

Une vérification sous confiance de test ne constitue pas une signature de
production Microsoft ni une certification HVCI/DRM. Enregistrer aussi les
diagnostics de politique noyau `/kp`. La qualification de chargement appartient
à la VM, pas au simple résultat Authenticode.

## Préparation Windows dans la VM seulement

Installer le certificat public de laboratoire dans les magasins locaux requis
Root et TrustedPublisher de la VM. Activer `bcdedit /set TESTSIGNING ON`, puis
redémarrer manuellement. Si Secure Boot empêche ce changement, traiter uniquement
le firmware virtuel de cette VM. Conserver l'état HVCI ; ne pas le désactiver
pour faire disparaître un échec. Le SYS doit posséder une signature de test
embarquée pour le scénario HVCI. Toutes ces mutations restent distinctes du
paquet unsigned et nécessitent l'environnement VM autorisé.

## Création du périphérique et installation

Depuis une console administrateur dans la VM préparée, après vérification du
paquet signé :

```powershell
.\tools\TimoxNativeLabDevice.exe inspect
.\tools\TimoxNativeLabDevice.exe create
.\tools\TimoxNativeLabDevice.exe create --apply
pnputil /add-driver ".\driver\TimoxWindowsAudio.inf" /install
```

La deuxième commande est un dry-run. La troisième crée une seule instance ROOT
et conserve un reçu protégé sous ProgramData ; elle n'installe pas l'INF.
PnPUtil ajoute le paquet et le lie au périphérique correspondant ; il ne crée
pas lui-même ce devnode ROOT. Conserver l'instance complète et le nom publié
`oemNN.inf` donnés par ces opérations. Une erreur à une étape interdit d'annoncer
l'installation réussie. Ne pas répéter des créations pour contourner un échec.

Vérifier absence d'erreur PnP, service `TimoxWindowsAudio` et les endpoints
`Timox Windows Input` (render application vers moteur) et `Timox Windows Output`
(capture moteur vers application). Aucun changement de périphérique par défaut
n'est nécessaire.

## Tests manuels progressifs

```powershell
.\probes\NativeAudioDriverProbe.exe --exercise-control
.\probes\NativeAudioLabProbe.exe --native --seconds 10
```

Le premier teste le contrôle privé ; le second ouvre le transport sans routes.
Ils ne prouvent pas qu'une application WASAPI a émis ou reçu des samples.
Exécuter ensuite explicitement `--native --seconds 10 --loopback-audio` avec
les endpoints choisis dans les applications de lecture/capture de la VM et
des motifs distincts gauche/droite. Le routage laboratoire applique -6,0206 dB
à gauche et l'unité à droite. Garder les captures et les compteurs.

Tester le refus du contrôle privé sous compte standard, ouverture concurrente,
arrêt/reprise, crash du probe, retrait/reboot et reconnexion. Driver Verifier
est une étape distincte, limitée à `TimoxWindowsAudio.sys` avec console et
snapshot disponibles. Les contraintes intersessions et de PCM protégé restent
à qualifier séparément ; l'ouverture réussie de SSH ou du contrôle ne les prouve pas.

## Retrait et récupération

Arrêter les probes puis, dans la console VM :

```powershell
.\tools\TimoxNativeLabDevice.exe remove
.\tools\TimoxNativeLabDevice.exe remove --apply
pnputil /delete-driver oemNN.inf /uninstall
```

Remplacer `oemNN.inf` uniquement par le nom enregistré pour ce paquet. L'outil
ne retire que l'instance du reçu validé ; un reçu absent/corrompu ou une instance
étrangère impose une inspection manuelle, sans adoption automatique. Le code
3010 demande un reboot manuel ; il ne prouve pas la disparition immédiate.
Ne pas utiliser de wildcard, `/force` ou suppression directe dans DriverStore.

Conserver `%WINDIR%\inf\setupapi.dev.log`, événements CodeIntegrity, résultats
JSON et dumps avant restauration du snapshot. Si restauration manuelle : retirer
seulement le certificat laboratoire identifié, puis rétablir la configuration
de test de la VM enregistrée avant intervention. Ne pas déclarer un rollback
complet si une de ces étapes manque ou demande un redémarrage.

## Références

[Signature de test et HVCI](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option),
[catalogue de test](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/test-signing-driver-packages),
[vérification SignTool](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/verifying-the-test-signature),
[PnPUtil](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).
