# Signature laboratoire VM : plan d'implémentation

> Exécution par lots avec `superpowers:executing-plans` et tests avant production.

Objectif : transformer une copie vérifiée du paquet unsigned en paquet de test
pour Windows 11 Enterprise LTSC 2024 x64 (build 26100). Aucune installation,
création de certificat, modification de confiance ou configuration de démarrage
dans le script. Le parent exécute seul les opérations utilisant la clé privée.

## Contrat et fichiers

- Nouveau `native-windows-audio/scripts/sign-vm-lab-package.ps1` : entrée
  `UnsignedDirectory`, nouvelle `OutputDirectory`, `CertificateThumbprint`,
  `SignToolPath`, `Inf2CatPath`. Aucune recherche approximative de certificat.
- Nouveau `vm-lab-signing.psm1` : orchestration injectable des outils et preuves,
  indépendante des clés pour les fixtures. Publication d'un candidat seulement
  après succès des commandes ; conserver les journaux en cas d'échec.
- Nouveau `tests/vm_lab_signing_tests.ps1` : fixtures de transaction sans signature.
- Aucun nouveau vérificateur cryptographique : vérification finale Microsoft
  dans la VM après installation de sa confiance. Le candidat doit porter
  `state=signed-candidate`, `vmVerified=false`, jamais `verified=true`.

## Ordre exact

1. Exécuter le vérificateur unsigned existant ; verrouiller la liste des fichiers
   et empreintes. Refuser dossiers identiques, imbriqués, existants en sortie,
   reparse points, UNC et fichiers supplémentaires. Copier exclusivement les
   fichiers listés dans un nouveau dossier ; recontrôler leurs empreintes.
2. Vérifier les exécutables Microsoft et leurs hashes ; sélectionner exactement
   le certificat `Cert:\CurrentUser\My\<thumbprint>` : clé présente, date valide,
   EKU code signing `1.3.6.1.5.5.7.3.3`, usage signature. Exiger clé RSA CNG avec
   politique d'export nulle dans le premier lot ; refuser fournisseur inconnu,
   exportabilité ou vérification impossible. Pas de tentative d'export privé.
3. Signer la copie du SYS ; régénérer son catalogue ; signer le CAT :

```powershell
& $SignToolPath sign /v /s My /sha1 $CertificateThumbprint /fd SHA256 $sys
& $Inf2CatPath ('/driver:' + $driverDirectory) /os:10_GE_X64 /verbose
& $SignToolPath sign /v /s My /sha1 $CertificateThumbprint /fd SHA256 $cat
```

   Vérifier chaque code de sortie immédiatement. Le catalogue antérieur est
   retiré uniquement dans la nouvelle copie, après identification exacte.
   Sans timestamp réseau dans ce lot : validité limitée au certificat de test.
   `10_GE_X64` est l'identifiant documenté pour Windows 11 24H2 ; relever la
   version réelle VM avant exécution, sans déduire son OS de l'INF.
   [Référence Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat).
4. Exporter `signer.cer` au format certificat public seulement. Vérifier égalité
   exacte DER avec le certificat sélectionné et SHA-256 ; aucun PFX/clé privée.
5. Sur l'hôte, relever structure, hashes et certificat signataire exact SYS/CAT.
   Cela ne prouve pas la validité de confiance ou l'appartenance au catalogue.
   Reporter ces contrôles SignTool complets dans la VM.
6. Produire `manifest-signed.json` : hash du manifeste unsigned d'origine,
   hashes avant/après SYS/CAT, liste exhaustive finale, certificats publics,
   horodatages UTC, hashes outils/journaux, cible OS et état des vérifications.
   Conserver le manifeste unsigned comme preuve historique, jamais comme
   manifeste applicable aux fichiers modifiés. Le nouveau manifeste est un
   inventaire des fichiers signés, pas un document lui-même signé. Le hash de
   l'archive et celui du certificat public sont communiqués séparément.
   Les logs doivent être figés avant leurs hashes et le manifeste.

## Vérification sans confiance sur l'hôte

`signtool verify /pa /v /c <cat> <sys|inf>` vérifie une politique de confiance,
pas uniquement une signature mathématique. Microsoft décrit la vérification
de test après ajout du certificat à la racine de confiance sur le **poste de
test**. `/r` ne constitue pas un magasin de confiance privé. Ne jamais assimiler
exit 1 ou `CERT_E_UNTRUSTEDROOT` à une preuve complète : une erreur peut masquer
un autre défaut. [Vérification Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/verifying-the-test-signature),
[options SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

La livraison interne du candidat ne dépend pas d'une confiance installée sur
l'hôte. Aucun contournement ni CMS maison. Dans la VM, contrôler le certificat
public épinglé avant import, puis exiger succès de `signtool verify /pa /v` sur
SYS et CAT, `/pa /v /c CAT` sur INF et SYS, et conserver séparément les résultats
`/kp`. État `VMverified` uniquement après ces contrôles de signature ; il ne
signifie pas que le pilote a été chargé ou que l'audio fonctionne.

## Lots et tests

- [ ] RED : contrat d'ordre des commandes et arrêt au premier échec ; faux
  certificat (absent/expiré/EKU/clés exportables), source modifiée, reparse,
  sortie existante ; statut vérification VM toujours faux sur l'hôte.
- [ ] GREEN : module injectable puis wrapper réel avec aucun store modifié.
- [ ] RED/GREEN : manifeste candidat, modifications non autorisées de membres,
  mauvais signataire ; fixture d'échec à chaque commande sans étape suivante.
- [ ] Revue indépendante avant signature réelle par le parent.
- [ ] VM : après confiance laboratoire explicitement installée sur la VM,
  SignTool doit réussir les quatre contrôles du runbook, puis qualification PnP,
  HVCI et audio. Une signature valide ne remplace aucune de ces qualifications.

Décision parent : livrer le candidat signé, explicitement non qualifié, puis
vérifier avec les outils Microsoft dans la VM. États distincts :
`unsigned-laboratory` → `signed-candidate` → `VMverified` ; aucune création de
clé, confiance, connexion ou installation ne découle de ce script.

## Reprise du 23 octobre 2026 : exécution et journaux

- Dépôt confirmé, branche `ccr-d306084d-ieaii9`, arbre propre au démarrage.
- Parser Windows PowerShell 5.1 : module, wrapper et fixtures sans erreur.
- Extraction de l'appel natif dans `Invoke-TimoxSigningCommand`, utilisé par
  le wrapper. Création du journal avant lancement, erreurs d'écriture
  terminantes même sous `ErrorActionPreference=Continue`, refus d'un code
  natif absent ou non nul, journal conservé pour les commandes silencieuses.
- Défaut reproduit : un append refusé pouvait être ignoré puis la commande
  déclarée réussie. Test avec journal rendu en lecture seule par la commande :
  échec observé sans `Add-Content -ErrorAction Stop`, succès après restauration.
- Tests exécutés sous Windows PowerShell 5.1 : `vm_lab_signing_tests.ps1` et
  `vm_lab_package_tests.ps1`, zéro échec. Capture stdout/stderr et arrêt sur
  code non nul exercés avec commandes locales sans certificat.
- Revue indépendante ciblée : aucun défaut fonctionnel signalé ; son manque
  de couverture sur l'append est corrigé par le test ci-dessus.
- Limite : les fixtures de séquence exercent le helper dans une boucle de
  test, pas la transaction complète du wrapper. Certificat (dont KeyUsage),
  provenance/copie, signataire et manifeste restent à couvrir avant signature.
- Aucune signature, création de clé, modification de confiance ou installation.

### Métadonnées certificat et copie épinglée — suite du 23 octobre

- L'utilisateur indique ne pas posséder de certificat. Aucun certificat ni clé
  créé. La commande proposée pour une identité auto-signée RSA CNG non
  exportable est documentée dans `docs/native-audio-vm-lab-runbook.md` ; elle
  n'a pas été exécutée et ne remplace pas une signature de distribution publique.
- `Assert-TimoxSigningCertificate` est utilisé par le wrapper : pin exact,
  validité, indication de clé privée, EKU code signing et KeyUsage
  DigitalSignature explicites, refus des extensions dupliquées/malformées.
  Les fixtures contrôlent les métadonnées et les décodeurs X509 .NET, pas une
  vraie clé de signature. Le contrôle RSA CNG réel reste dans le wrapper.
- La copie utilise désormais un snapshot du manifeste vérifié, puis revalide
  la source et les empreintes avant copie ; seuls les membres épinglés sont
  copiés et le résultat passe le vérificateur unsigned. Tests de mutation du
  fichier/manifeste, ajout de fichier, sortie existante/imbriquée et jonctions.
- Revue indépendante : aucun défaut bloquant ; incohérence de casse SHA-256
  signalée puis corrigée avec test RED/GREEN. Décision : traiter ce rejet d'une
  entrée acceptée par le vérificateur comme défaut fonctionnel du nouveau helper.
- Windows PowerShell 5.1 : certificats 18 contrôles/0 échec, suites signature
  et paquet 0 échec, parsing réussi. Aucune preuve de signature réelle,
  chargement noyau ou audio ne découle de ces tests.
- Restant avant signature : transaction complète injectable, validation du
  signataire et inventaire final/DER public, scénarios d'échec du wrapper et
  choix autorisé de l'identité laboratoire. Les chemins sont contrôlés mais
  aucune exclusion d'un écrivain malveillant concurrent n'est revendiquée.

### Transaction complète et outils — suite du 23 octobre

- `Invoke-TimoxSigningTransaction` est maintenant appelé par le wrapper réel.
  Il copie le snapshot vérifié, exécute SYS/Inf2Cat/CAT, contrôle les signataires
  par empreinte ET DER exact, conserve les membres inchangés, exporte seulement
  les octets publics, refuse tout membre inattendu ou reparse et épingle les
  hashes des outils avant/après. Publication du manifeste par renommage d'un
  fichier temporaire après tous les contrôles ; erreurs gardées pour diagnostic.
- 14 scénarios de transaction avec adaptateurs simulés : succès, échec de
  chacune des commandes, exception native, mauvais pin/DER/status, INF modifié,
  fichier supplémentaire, outil modifié, journal/catalogue manquant et jonction.
  Le succès vérifie tous les hashes du manifeste et `vmVerified=false`.
- Revue indépendante favorable. Trois suites PS5.1 réussies et parsing du
  module/wrapper/fixture réussi. Les adaptateurs ne prouvent ni Authenticode
  réelle ni catalogue ni clé CNG. Aucune signature réelle exécutée.
- Paquet réel unsigned revalidé : 12 membres, manifeste SHA-256 toujours
  `EA17A976C368F566F0A731891C173B54ABDA488BDC6C7D3CEF0E313FD2473CBC`.
- Obstacle réel à traiter avant création de clé : Inf2Cat du WDK 28000.2526
  est signé par `Windows Internal Build Tools CodeSign` avec racine
  `Microsoft Services Partner Root`, non reconnue par l'hôte. Authenticode
  renvoie UnknownError et SignTool `/pa /v` échoue (racine non approuvée).
  Ne pas importer cette racine ni supprimer le contrôle du wrapper.
- WDK officiel 26100.6584 téléchargé (accès réseau approuvé) dans
  `build_native_audio_wdk/signing-tools-26100/wdk-10.0.26100.6584.nupkg` :
  SHA-256 `C393D03DFB640B5C92F546B32F6770EF68CD3AAF691956E7D66D8E2C28A1B55E`.
  Extraction locale `package/`; son Inf2Cat `c/bin/10.0.26100.0/x86/Inf2Cat.exe`
  (date UTC 2025-09-05 12:43:30) a le SHA-256
  `B594728D38B271979367ABC8060A971B8E42422738009BE126710B1F5DD0FCBC`, avec
  le même rejet de racine. Aucun outil installé dans Windows.
- `build_native_audio_wdk/nuget.exe verify -Signatures <nupkg> -Verbosity detailed`
  exécuté hors sandbox après refus d'accès à NuGet.Config : code 0, signatures
  auteur Microsoft et dépôt NuGet vérifiées. Certificat auteur SHA-256
  `566A31882BE208BE4422F7CFD66ED09F5D4524A5994F50CCC8B05EC0528C1353`.
  Cette preuve porte sur le paquet signé, pas sur la confiance Authenticode
  individuelle d'Inf2Cat. Aucune implémentation alternative du contrôle des
  outils n'est encore ajoutée : prochaine étape, lier vérification officielle
  NuGet avec identité Microsoft épinglée et contenu extrait exact, puis revoir
  ce chemin avant usage. Ne pas transformer UnknownError en succès.

### Provenance Inf2Cat depuis la distribution Microsoft épinglée

Décision : pour cette archive précise, la preuve d'origine est la signature
NuGet Microsoft, vérifiée par le client officiel, puis le SHA-256 de l'archive
complète épinglé dans le code. Le statut Authenticode individuel n'est ni
modifié ni déclaré valide. Le paramètre explicite `WdkPackagePath` active ce
chemin ; sans ce paramètre, le contrôle Authenticode des outils reste requis.
Cette exception est limitée au paquet 26100.6584 identifié ci-dessus. Une
mise à jour du paquet demande une nouvelle preuve et une revue du pin.

- Commande réelle NuGet avec `-CertificateFingerprint
  566A31882BE208BE4422F7CFD66ED09F5D4524A5994F50CCC8B05EC0528C1353`,
  `-NonInteractive -ForceEnglishOutput -Verbosity detailed` : code 0.
  Journal `build_native_audio_wdk/signing-tools-26100/nuget-author-verification.log`,
  SHA-256 `53E9795CC85478D7C69F300D2A24C6B03DBB7BE4368AA02C3EE07AFAA66E499C`.
- L'identité auteur est confirmée par la [publication Microsoft du 23 septembre
  2026](https://devblogs.microsoft.com/dotnet/microsoft-author-signing-certificate-update-2026/).
  La vérification suit [NuGet verify](https://learn.microsoft.com/en-us/nuget/reference/cli-reference/cli-ref-verify).
- `Get-TimoxPinnedInf2Cat` exige le hash exact du paquet et compare tout le
  dossier x86 d'Inf2Cat à son contenu ZIP : EXE, DLL et XML. Aucun ajout,
  suppression, modification ou reparse n'est accepté. La vérification est
  répétée autour des appels natifs ; la provenance figure dans le manifeste.
- Tests PS5.1 de l'inventaire : archive correspondante, archive non autorisée,
  DLL modifiée/ajoutée/manquante et jonction. Quatre suites vertes ; parsing
  réussi ; comparaison du vrai dossier Inf2Cat au vrai paquet réussie.
- Ces contrôles ne constituent pas une sandbox contre un écrivain concurrent
  malveillant et ne changent pas l'état de confiance de Windows.

### Candidat réellement signé — 23 octobre 2026

- Revue indépendante du chemin de provenance favorable. Après autorisation,
  identité créée dans CurrentUser/My, RSA CNG 3072 non exportable vérifié,
  EKU/KeyUsage et dates contrôlés. Subject `CN=Timox Windows Audio VM Lab 2026-10-23`.
  Empreinte `395F8ED6F0271D7C94D5E1AAFD663B6999E13FA9` ; expiration UTC
  `2027-04-23T16:40:05Z`. Aucune confiance Root/TrustedPublisher installée.
  Réutiliser cette identité ; ne pas créer une nouvelle clé à la reprise.
- Enregistrement public local :
  `build_native_audio_wdk/signing-tools-26100/lab-certificate.json`.
  Aucun PFX exporté ; certificat public SHA-256
  `B5405878149BDB26FF7FCA683171650F5750C5AA8669C33DF26E6B769CF4720D`.
- Premier essai dans `build_native_vm_signed_candidate_2026-10-23` : trois
  commandes Microsoft réussies, mais arrêt avant manifeste car PowerShell
  signale la racine de test non approuvée comme UnknownError. Répertoire gardé.
- Diagnostic réel exactement égal au message Windows pour HRESULT
  `CERT_E_UNTRUSTEDROOT` (-2146762487). Décision : accepter uniquement ce
  diagnostic exact comme observation d'un candidat non qualifié, sans en
  déduire validité de signature ou appartenance catalogue. Pin et DER exacts
  restent requis. UnknownError générique et HashMismatch restent refusés.
  Tests RED/GREEN, observations du manifeste assertées, revue indépendante
  favorable. La transaction couvre désormais 16 scénarios.
- Deuxième essai, même clé et nouvelle copie :
  `build_native_vm_signed_candidate_2026-10-23-r2`.
  SignTool SYS et CAT : succès, 0 avertissement/erreur ; Inf2Cat : génération
  terminée, aucun avertissement/erreur. Wrapper PS5.1 code 0, manifeste publié
  avec `state=signed-candidate`, `vmVerified=false`, diagnostics explicites.
- Inventaire réel vérifié : 18 fichiers, chaque taille/hash conforme ; export
  public lu comme X509 sans clé privée ; chaque entrée ZIP comparée au dossier.
  Archive `build_native_vm_signed_candidate_2026-10-23-r2.zip`, 801657 octets :
  SHA-256 `5FA1533D128832A792F07C87343394D4F80E5E053917E41DE8BA795D19778CF7`.
  Manifeste final SHA-256
  `2EE86942F342D6A0521EC12B065E06ECA70A4F2AD8279AB2A3736E001BB9AC8E`.
- Toujours NON qualifié en VM : contrôles SignTool après confiance de test,
  appartenance INF/SYS au CAT, politique noyau, chargement/PnP/HVCI et audio
  encore requis. Aucune installation de pilote sur l'hôte. Adresse SSH actuelle
  de la VM demandée à l'utilisateur ; aucune réponse reçue à cet instant.
