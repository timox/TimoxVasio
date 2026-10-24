# Qualification du SID de service et de la publication

`TimoxNativeAudioServiceProbe.exe` est un outil pour une VM Windows de laboratoire,
pas le broker audio de production. Aucun flux audio, listener, certificat ou clé
n'est créé. Le mode SCM écrit des métadonnées synthétiques dans
`ProgramData\TimoxNativeAudioBroker\Qualification\discovery.bin` ; les clients de
production consultent uniquement le sibling `Runtime`.

L'hôte de développement n'a exécuté que les tests et les modes sans écriture.
Ni la préparation `--apply` ni l'installation/exécution SCM ne sont qualifiées.
Un snapshot de la VM permet de conserver un état initial reproductible.

## Artefact et lecture seule

Construire la cible `TimoxNativeAudioServiceProbe` dans build_native_production.
Comparer chemin, date et SHA-256 du fichier transféré à ceux du manifeste du
candidat. L'exécutable x64 dépend du runtime Microsoft Visual C++ v14 x64 adapté
au toolset utilisé ; vérifier `--help` dans la VM avant toute préparation SCM.
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

## Exécution privilégiée à autoriser dans la VM

Ces commandes ne font pas partie des tests automatiques. Elles nécessitent un
terminal administrateur dans la VM et un exécutable vérifié placé dans un
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

Références : [SID de service](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_sid_info),
[attributs de groupe](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_groups),
[ServiceMain](https://learn.microsoft.com/en-us/windows/win32/services/writing-a-servicemain-function).
