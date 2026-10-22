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
