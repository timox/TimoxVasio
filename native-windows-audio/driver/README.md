# Prototype natif Timox Windows Audio — N1/N2

Ce projet produit un véritable binaire noyau `TimoxWindowsAudio.sys`, dérivé
du sous-ensemble PortCls/WaveRT de SysVAD. Il est distinct de `TimoxVasio.dll`.
Le build Release x64 passe avec W4/WX, InfVerif et ApiValidator Universal.
Il n'a pas été chargé : l'énumération et le fonctionnement WASAPI restent à
qualifier sur une machine de test. Le catalogue généré n'est pas signé.

Deux endpoints sont décrits, au format PCM signé 32 bits, stéréo, 48 kHz :

| Endpoint Windows | Sens | Comportement N1 |
|---|---|---|
| Timox Windows Input | application → render WaveRT | audio consommé puis abandonné |
| Timox Windows Output | capture WaveRT → application | silence initialisé explicitement |

Ces comportements N1 sont désormais prolongés par le contrôle privé N2 :
le rendu remplit une file bornée lisible par le moteur ; les paquets capture
validés alimentent la sortie Windows, avec silence sur les frames manquantes.
Ce raccordement est compilé mais **aucun échange noyau réel n'a été exécuté**.

Chaque endpoint expose un stream physique ; le partage entre applications
revient au moteur audio Windows. Aucun APO, offload, pin loopback, pilote
Bluetooth/USB ou générateur de sinus n'est inclus. Les buffers sont initialisés
et le remplissage du silence est borné à un tour de ring. Le timing et les
transitions WaveRT hérités du sample nécessitent encore qualification.
Le ring est limité à une seconde (384000 octets), avec des paquets alignés
sur les frames et une période de notification minimale de 1 ms. Le format
entrant doit déclarer un débit de 384000 octets/s et une extension contenue
dans la taille fournie. Ces règles partagées pilote/hôte sont exercées par
`NativeAudioGeometryTests`, en complément des tests du protocole N0.

```mermaid
flowchart LR
  A[Applications WASAPI] --> R[Timox Windows Input / render]
  R --> D[Consommation sans transport N1]
  S[Silence N1] --> C[Timox Windows Output / capture]
  C --> A
```

L'identité PnP est `Root\TimoxWindowsAudio`, le service `TimoxWindowsAudio`
et les références KS sont `TimoxRenderWave`, `TimoxRenderTopology`,
`TimoxCaptureWave`, `TimoxCaptureTopology`. Les GUID de catégories KS restent
ceux de Windows ; ce ne sont pas des identités propriétaires à remplacer.
Le GUID du projet est propre à Timox. Le contrôle N2 utilise la classe privée
`20ec34c0-1f92-41b0-a59e-fc898f742f64` pour ses paramètres de sécurité.
La licence et le commit amont sont dans `THIRD_PARTY_NOTICES.md`.

## Reproduire le build

Préparer les dépendances épinglées avec les scripts décrits dans
[`../BUILDING.md`](../BUILDING.md), puis depuis la racine :

```powershell
git rev-parse --show-toplevel
git status --short
& 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe' `
  native-windows-audio/driver/TimoxWindowsAudio.vcxproj `
  /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /m:1
```

Sortie : `build_native_windows_audio/x64/Release/TimoxWindowsAudio.sys`.
Ce build n'installe rien. La date INF est estampillée par WDK : reproductible
désigne ici la procédure et les dépendances, pas une identité binaire garantie.

Preuve locale N2 du 22 octobre 2026 à 15:54:16 UTC : 76 288 octets, SHA-256
`A3F8B95FFF414FC00F6B04960E84EDF28B1C4D28C6027E6770062FF49D1776A3`.
Le journal conservé est `build_native_audio_wdk/timox-driver-n2-eos-final.log`.
Cette empreinte décrit le build observé ; un rebuild peut la changer.

## Contrôle privé N2

`control.cpp` crée un device distinct de l'audio public, classe GUID propre,
ACL laboratoire SYSTEM/Administrateurs, accessible via
`\\.\Global\TimoxWindowsAudioControl`. Un seul adaptateur est permis, comme
dans le sous-ensemble SysVAD retenu. Tous les majors privés passent par le
dispatch Timox ; les majors audio conservent leurs handlers d'origine.
Un seul FILE_OBJECT possède la session ; CLEANUP purge ses files et la
génération change au prochain open. Les IOCTL METHOD_BUFFERED sont non
bloquants, avec un maximum de 32832 octets et droits lecture+écriture.

Le contrat exact est dans `../include/native_audio_control_protocol.h`.
La capture rejette les séquences répétées/décroissantes ; un paquet refusé
pour saturation peut être retenté. Render vide retourne NO_MORE_ENTRIES sans
paquet artificiel. Le premier paquet et ceux suivant une perte portent un
flag de discontinuité. Les timestamps QPC des paquets render ne sont pas
encore publiés (flag QPC_VALID absent).

La progression WaveRT est calculée en frames entières à partir du temps
monotone, avec reste fractionnaire et arithmétique 64 bits. Un retard supérieur
au ring ne provoque pas une boucle proportionnelle au retard : seul le dernier
tour est traité, les files anciennes sont purgées et les frames perdues sont
comptées. Les notifications suivent les paquets complets, avec un compteur
notifié distinct. Les positions et fins de flux doivent être alignées sur
8 octets ; une fin exactement à la limite du ring est traitée une seule fois.

Le transport privé n'est pas une sortie DRM authentifiée. CopyProtect,
DigitalOutputDisable ou un échec de mise à jour des droits bloquent le rendu
privé et purgent sa file. Le blocage persiste jusqu'à destruction du stream,
même si ses droits changent ensuite ou si le moteur ferme/réouvre son handle :
cela empêche de publier des données protégées restées dans le DMA.
La destruction attend le timer, purge puis libère le blocage **avant** de
libérer le slot de stream, afin qu'un nouveau stream non protégé puisse repartir.
Cette politique conservatrice ne constitue pas une certification DRM.
Sources : [droits de contenu Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/content-ids-and-content-rights),
[contrat SetWritePacket](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/portcls/nf-portcls-iminiportwavertoutputstream-setwritepacket),
[timestamp du premier sample GetReadPacket](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/portcls/nf-portcls-iminiportwavertinputstream-getreadpacket).

La protection rundown interdit les nouveaux transferts au retrait et attend
les références existantes hors callback. Le verrou court sérialise session
et files. Le contexte est dans l'extension du device, conservée par les
références FILE_OBJECT/IRP après IoDeleteDevice. Le probe
`build_native_audio_protocol/Release/NativeAudioDriverProbe.exe` est compilé,
mais son option `--exercise-control` reste **non exécutée** : elle requiert
un pilote installé sur une cible de test. Il ne charge ni n'installe rien.

## Travail restant

Qualifier les points `ReadBytes` (render) et `WriteBytes` (capture), le contrôle
sécurisé, la durée de vie des buffers, la déconnexion et les transitions
stop/reprise sur cible. Préparer le broker moteur, les timestamps et les
compteurs complets de pertes hors session ; le wrapper actuel expose des
compteurs limités à la session et aux files, pas une métrologie complète.
L'ACL audio publique de ce prototype ne doit pas servir à ouvrir le contrôle
privé : ce dernier devra autoriser exclusivement le service moteur prévu.

La conversion float/PCM32 et la compensation de dérive restent dans le moteur
utilisateur. Avant tout déploiement : revoir les chemins à IRQL élevé, les
annulations/PnP/power, puis tester en VM adaptée avec Driver Verifier, ouverture
WASAPI duplex, silence, pause/reprise, retrait et reprise après crash du moteur.
Tester aussi les requêtes KS avec PinId 0, 1, 2 et UINT_MAX, les tailles de
propriété tronquées et les échecs de mapping. Les handlers et prédicats bornent
désormais les indices ; cela ne constitue pas une validation dynamique de
sécurité. Les gardes de buffer vide évitent les modulos par zéro mais ne
remplacent pas la synchronisation du retrait du mapping avec les callbacks.
La signature et l'installation constituent un lot séparé ; aucune réussite
de build ne démontre leur disponibilité sur cette machine.
