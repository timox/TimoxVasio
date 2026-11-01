# Caractériser un son géré par Windows

**Statut : modèle de travail en cours lié aux préversions non publiées.** Ce
document ne correspond à aucune release ou fonctionnalité stable publiée.

État du 27 octobre 2026. Exemple : Firefox, sa sortie Windows et sa copie vers
Timox. Ce document rassemble le modèle d'observation et les écarts de couverture ;
il ne prétend pas que tous les champs sont déjà exposés par l'API.

## Un trajet, plusieurs périmètres

```text
Processus et ses enfants → sessions/flux Windows → mixeur de l'endpoint → sortie
              └→ copie de l'arbre de processus → pont Timox → graphe → sortie ASIO
```

Une session Windows regroupe des flux ; un processus peut avoir plusieurs
sessions, et certaines sessions couvrent plusieurs processus. Une session
de rendu est associée à un endpoint. La capture par processus vise l'arbre
sélectionné, indépendamment d'un endpoint particulier : ses sorties Windows
observées sont un contexte, pas la cible technique de ce pont.
Sources : [sessions Windows](https://learn.microsoft.com/en-us/windows/win32/coreaudio/audio-sessions),
[capture par processus](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/).

## Fiche à rassembler

| Périmètre | Caractéristiques | Couverture actuelle Timox |
|---|---|---|
| Processus | Nom exécutable, PID, date de création, chemin ; processus sélectionné et périmètre enfants | Identité exposée ; capture de l'arbre. Le nom seul sert au modèle, jamais à authentifier une instance. |
| Session Windows | Identifiant de session et d'instance, PID déclaré, portée multi-processus, état actif/inactif/expiré, nom d'affichage | L'inventaire actuel agrège les sessions actives mono-processus ; pas de fiche session complète dans l'API. |
| Sortie d'origine | Endpoint ID opaque, nom, sens, état, appareil/conteneur si disponible ; rôle par défaut distinct du choix effectif | Inventaire endpoints disponible. `windowsAudioApplications.renderEndpoints` ne contient actuellement que les noms des sorties, pas leurs IDs : insuffisant pour une jointure fiable sur les appareils. |
| Format | Fréquence, nombre et position des canaux, PCM entier/flottant, bits conteneur/utiles, taille d'une trame | Format de mixage de l'endpoint et format effectif du pont exposés séparément. Format original de chaque flux Firefox inconnu. |
| Niveaux et commandes | Volume/mute de session, volumes par canal si accessibles, volume/mute endpoint, pics et périmètre exact du vumètre | Volumes/mutes Windows non exposés par l'API actuelle ; sondés ponctuellement pour le diagnostic. Pics des ports Timox exposés. RMS/LUFS nécessitent une mesure appropriée, pas une conversion du volume de réglage. |
| Temps et buffers | Horloge moteur, taille de bloc/période, buffer natif, latence déclarée, FIFO, ratio de rééchantillonnage et correction | Mesures moteur/pont disponibles. Une latence non fournie n'est pas une latence nulle. La latence acoustique de bout en bout n'est pas mesurée. |
| Continuité | Trames reçues/rejetées, silences ajoutés par Timox, discontinuités, erreurs de timestamp, retries, erreur native | Plusieurs compteurs déjà présents. Les trames marquées silencieuses par Windows ne sont pas comptées séparément aujourd'hui. |
| Topologie | Trajet direct, copie, destinations Timox, gains/mutes, points de sommation et chemins parallèles | Liens internes observables. Correspondance endpoint Windows/canal ASIO et mélange matériel SSL pas établis automatiquement. |
| Provenance | Source de la mesure, identité/génération, date d'acquisition, fenêtre, unité, disponibilité/erreur | Couverture partielle : l'heure de lecture API n'est pas une preuve d'heure d'acquisition. Aucun zéro de remplacement pour une valeur inconnue. |

Les identifiants d'instance de session distinguent les instances ; un PID seul
peut être réutilisé et une session peut être multi-processus.
Sources : [GetSessionInstanceIdentifier](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nf-audiopolicy-iaudiosessioncontrol2-getsessioninstanceidentifier),
[GetProcessId](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nf-audiopolicy-iaudiosessioncontrol2-getprocessid).

## Ne pas confondre ces valeurs

- **Format source application**, **format du mixeur Windows**, **format de la
  capture** et **format du graphe Timox** sont quatre informations distinctes.
  `GetMixFormat` décrit le traitement partagé Windows, pas le fichier lu ni
  nécessairement le format matériel. Voir [GetMixFormat](https://learn.microsoft.com/en-us/windows/desktop/api/audioclient/nf-audioclient-iaudioclient-getmixformat).
- Un volume réglé à 1 n'est pas une mesure à 0 dBFS. Un vumètre endpoint
  Windows observe avant son contrôle de volume. Sans vumètre matériel, sa
  mesure logicielle ne couvre pas un flux exclusif ; zéro ne garantit donc
  pas l'absence de son sur tous les chemins. Voir [IAudioMeterInformation](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudiometerinformation).
- Un processus vivant, une session active, un pont qui échange et un signal
  audible sont quatre états différents. Une capture peut échanger du silence
  sans underrun. Le compteur `silencedFrames` de Timox concerne son FIFO, pas
  tous les silences fournis par Windows ou par l'application.
- Deux sorties portant « SSL » ne prouvent ni le même port ni une sommation.
  La copie et le trajet direct doivent être distingués ; un doublon externe
  reste suspect tant que leur point de recombinaison n'est pas observé.

## Exemple mesuré, pas une configuration permanente

Pendant le défaut signalé : Firefox PID 17444, sortie observée Windows
« Output 5/6 (SSL 12 USB Audio Device) », pont en PCM16 stéréo 44,1 kHz,
graphe/horloge ASIO SSL à 48 kHz avec blocs de 2048 trames. Le graphe envoyait
la copie vers SSL 1/2 et 3/4 à gain unitaire.

Plus tard, cette instance n'était plus disponible ; une autre session Firefox
était inactive sur Output 3/4. Ces relevés ne doivent pas être fusionnés en une
seule identité ou une seule configuration supposée constante. Les preuves et
limites figurent dans le [diagnostic Firefox/SSL](plans/2026-10-27-firefox-ssl-audio-diagnostic.md).

## Rattachement au modèle Timox

Le processus est un adhérent ; le pont constitue son accès par un nœud.
Les sessions et endpoints Windows décrivent cet accès et son environnement,
sans créer automatiquement de nouveaux adhérents. Le moteur porte les
observations et les liens, avec leur provenance.

Le catalogue conserve une intention réutilisable : nom du processus et
paramètres du pont. PID, session, sortie courante et mesures sont redécouverts
à l'application du modèle. Voir [le lot catalogue](plans/2026-10-27-process-copy-catalogue.md).

Priorités de supervision à concevoir : IDs des endpoints d'origine et fiches
de session, volumes/mutes et pics Windows correctement situés, trames silencieuses
reçues de Windows et mesure avant/après conversion. Elles restent à implémenter ;
aucun contrôle automatique du volume ou du routage Windows n'est décidé ici.
