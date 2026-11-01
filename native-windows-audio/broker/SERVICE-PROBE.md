# Qualification du SID de service et de la publication

`TimoxNativeAudioServiceProbe.exe` est un outil de qualification Windows,
pas le broker audio de production. Aucun flux audio, listener, certificat ou clé
n'est créé. Le mode SCM écrit des métadonnées synthétiques dans
`ProgramData\TimoxNativeAudioBroker\Qualification\discovery.bin` ; les clients de
production consultent uniquement le sibling `Runtime`.

L'hôte de développement n'a exécuté que les tests et les modes sans écriture.
Ni la préparation `--apply` ni l'installation/exécution SCM ne sont qualifiées.
L'utilisateur a choisi de préparer les essais sur le poste Windows existant,
sans VM. Aucune VM n'est actuellement disponible. Avant une exécution SCM,
préparer les sauvegardes et le retrait limité aux objets créés par l'essai.
L'inventaire ci-dessous est sans installation et ne constitue pas cet essai.

```powershell
.\native-windows-audio\scripts\inspect-host.ps1
```

Ce script produit du JSON : présence des deux services, chemins, dates, hashes
et état Authenticode des binaires. Une erreur d'accès reste `unknown`, jamais
interprétée comme une absence. Il n'ouvre aucun endpoint audio. Le 24 octobre,
les deux services sont absents (1060), le SYS est présent et non signé.
Le probe `--inspect` observe une racine absente (2), et son dry-run confirme
`productMissingBefore=true`, `qualificationMissingBefore=true`. Ces résultats
ne qualifient pas le compte LocalService ni les écritures du futur service.

## Artefact et lecture seule

Construire la cible `TimoxNativeAudioServiceProbe` dans build_native_production.
Comparer chemin, date et SHA-256 du fichier transféré à ceux du manifeste du
candidat. L'exécutable x64 dépend du runtime Microsoft Visual C++ v14 x64 adapté
au toolset utilisé ; vérifier `--help` sur la cible avant toute préparation SCM.
Le candidat ne fournit pas d'installateur de runtime.

```powershell
.\TimoxNativeAudioServiceProbe.exe --help
.\TimoxNativeAudioServiceProbe.exe --inspect
.\TimoxNativeAudioServiceProbe.exe --prepare-qualification-root
```

`--inspect` produit un diagnostic JSON. Son exit code 0 signifie que l'observation
du jeton a réussi, pas que le système est prêt : lire `eligible`,
`qualificationRootError`, `publicationError` et `ownerError`. Sous un utilisateur
normal, eligible=false est attendu. Avant préparation, les erreurs de racine et
de publication valent normalement 2 (absent). Le dry-run ne crée rien.

La racine du broker est volontairement distincte de ProgramData\TimoxVasio,
dont l'outil de laboratoire existant exige une autre DACL. Aucune migration ou
modification silencieuse des droits de ce répertoire partagé n'est effectuée.

## Exécution privilégiée à autoriser sur la cible

Préparer d'abord le plan JSON avec le hash du candidat revu :

```powershell
.\native-windows-audio\scripts\plan-service-qualification.ps1 `
  -ExpectedSha256 C5C80695F16C090BD5F1FF2A512B1243871FC635ADA744E2BBF07BAF1E98C2E0
```

Ce préparateur vérifie le hash avant d'exécuter les deux modes de lecture du
probe, exige l'absence du service et fournit les arguments SCM, les critères
de réussite et le retour arrière. Il ne copie ni n'installe aucun fichier,
n'exécute aucune commande SCM d'écriture et ne modifie aucune ACL. Un rebuild
du probe demande de revoir son nouveau hash ; ne pas contourner ce contrôle.

Ces commandes ne font pas partie des tests automatiques. Elles nécessitent un
terminal administrateur sur la cible et un exécutable vérifié placé dans un
répertoire protégé sous Program Files. Ne jamais remplacer un service existant
nommé TimoxNativeAudioBroker : le probe utilise ce nom pour obtenir exactement
le SID prévu pour le futur broker.

Avant toute écriture, `sc.exe query TimoxNativeAudioBroker` doit confirmer
ERROR_SERVICE_DOES_NOT_EXIST (1060). Toute autre situation demande inspection.
Conserver le résultat du dry-run et le hash initial de discovery.bin s'il existe.

```powershell
$probe = 'C:\Program Files\TimoxNativeAudioServiceProbe\TimoxNativeAudioServiceProbe.exe'
& $probe --prepare-qualification-root --apply
```

Contrôler le code de sortie et le JSON avant de poursuivre. Apply crée uniquement
les dossiers absents avec propriétaire Administrateurs et DACL explicite :
SYSTEM/Administrateurs/SID du broker en contrôle complet, Authenticated Users en
lecture/traversée. Les objets existants non conformes sont refusés, jamais réparés.
Un échec peut laisser le premier dossier créé ; aucune suppression automatique
n'est tentée. Runtime n'est pas créé.

```powershell
sc.exe create TimoxNativeAudioBroker binPath= "`"$probe`" --service-publication-probe" type= own start= demand obj= 'NT AUTHORITY\LocalService'
sc.exe sidtype TimoxNativeAudioBroker unrestricted
sc.exe privs TimoxNativeAudioBroker SeChangeNotifyPrivilege
sc.exe qc TimoxNativeAudioBroker
sc.exe qsidtype TimoxNativeAudioBroker
sc.exe qprivs TimoxNativeAudioBroker
sc.exe start TimoxNativeAudioBroker
sc.exe query TimoxNativeAudioBroker
```

Vérifier chaque code de sortie séparément ; ne pas continuer après un échec.
Le service doit terminer de lui-même. Conserver le chemin configuré, le compte,
le SID type et les privilèges, puis l'état STOPPED et les deux codes de sortie.
Succès : WIN32_EXIT_CODE=0 et SERVICE_EXIT_CODE=0 après ce démarrage vérifié.
En erreur, WIN32_EXIT_CODE=1066 et le code spécifique identifie l'étape :

| Code | Étape |
|---|---|
| 1 | Lecture du jeton |
| 2 | Compte/session/SID non éligible |
| 3 | Ouverture de la racine de qualification |
| 4 | Publication |
| 5 | Relecture protégée |
| 6 | Propriétaire exact du fichier |
| 7 | Comparaison intégrale des métadonnées |
| 8 | Annulation demandée |
| 9 | Génération aléatoire |
| 10 | Publication du statut SCM |

L'outil tente aussi d'écrire l'étape et l'erreur Windows dans le journal
Application, source TimoxNativeAudioServiceProbe ; ce journal est best effort et
ne remplace pas le résultat SCM. Aucun enregistrement de source n'est installé.
L'événement inclut les attributs bruts `serviceGroupAttributes`, l'erreur de
lecture du jeton et le résultat du contrôle strict `eligible`, afin de trancher
le comportement réel du SID sans assouplir le contrôle au préalable.

Après l'arrêt, exécuter `--inspect` depuis un utilisateur non administrateur.
Exiger les erreurs racine/publication/propriétaire à zéro et un epoch non nul,
différent du précédent essai. Archiver le hash de discovery.bin, le JSON, les
résultats SCM et le hash de l'exécutable. Le fichier doit appartenir exactement
à `NT SERVICE\TimoxNativeAudioBroker`, pas seulement à LocalService.

Pour retirer le service de test après archivage, vérifier d'abord qu'il est
arrêté et que son chemin est toujours celui du probe, puis utiliser
`sc.exe delete TimoxNativeAudioBroker`. Conserver les métadonnées de qualification
jusqu'à archivage ; ne pas supprimer récursivement la racine du broker.

## Portée de la preuve

Un succès établit la présence effective du SID de service activé et éligible à
la propriété, ainsi que création/publication/relecture sous ce jeton. Il ne
qualifie ni CNG/Schannel, ni enrollment par logon, ni isolation du PCM, ni pilote,
ni performances, ni démarrage du véritable broker. L'outil refuse l'impersonation
et n'ajuste aucun privilège ou attribut de groupe pour faire passer le test.

Build du probe après ajout de ces champs, Release du 2026-10-24 19:48:56 UTC :
108032 octets, SHA-256
`F5C5A1EC8388A7B8A08B8025D9BEAF8B2F26FD26BC4274756B6011DCC1A51B57`.
Ce candidat n'a pas été exécuté ; aucun service ni fichier de qualification
n'a été créé.

Références : [SID de service](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_sid_info),
[attributs de groupe](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_groups),
[ServiceMain](https://learn.microsoft.com/en-us/windows/win32/services/writing-a-servicemain-function).
