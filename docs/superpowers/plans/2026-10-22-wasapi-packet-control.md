# Régulation WASAPI face aux paquets natifs

Le test matériel de dix minutes a montré une correction atteignant fréquemment
la borne de ±2000 ppm malgré des compteurs d'incidents stables. La régulation
réagissait aux oscillations de remplissage produites par les blocs audio.

La correction observe le milieu du remplissage avant/après un traitement natif
complet, conserve cette observation entre passes bornées et filtre la mesure.
Les gains et le filtre sont adaptés à la cadence du graphe. Un traitement qui
dure plus de 100 ms force une observation partielle, comptée séparément dans
`health.controlBacklogUpdates`. Ce compteur ne représente pas une perte audio.

Vérifications du 22 octobre 2026 : build Release moteur et trois tests natifs
réussi ; WasapiBridgeTests, DriftResamplerTests et WasapiBridgeApiTests réussis.
Les simulations couvrent 720 combinaisons de direction, fréquence, quantum,
paquet, phase et dérive. Résultat rapporté : erreur RMS maximale 79,8465 ppm,
erreur moyenne maximale 1,43474 ppm, marge minimale FIFO 26,0111 frames.
Les tests du worker couvrent le fractionnement des paquets et l'observation
forcée après 100 ms. Quatre messages HTTP natifs valident le schéma API ; les
73 tests GUI passent.

Ces résultats ne qualifient pas les petits quanta : le dimensionnement FIFO historique
peut être inférieur à un paquet natif converti et fait l'objet d'un lot séparé.
La DLL ASIO reste inchangée.

## Essai matériel de 60 secondes

Session locale `build_wasapi_e2e/session-eb743c480f9e4fb0ae4d5a2b743f6730/asio-engine-75e74c795bad400f9104c47fb60e2578`, le 22 octobre 2026 vers 06:01 UTC.
Chemin : émetteur ASIO → moteur logiciel 48 kHz / 2048 frames → WASAPI rendu
SSL 12 Output 3/4 → loopback du même endpoint → récepteur ASIO. Le buffer natif
effectif est de 1056 frames, float32 stéréo 48 kHz ; chaque FIFO contient 16384 frames.

- 2 880 000 frames analysées ; 2400/2400 fenêtres de présence des deux tonalités.
- Aucun échantillon nul/non fini, aucune demande de reset, aucune fenêtre rejetée.
- 55 observations après amorçage sur 59,9134577 s ; intervalle maximal 1,2156332 s.
- Aucun accroissement des sept compteurs d'incidents surveillés ; générations stables.
- Correction échantillonnée : rendu −942,68 à +1793,96 ppm ; loopback −1708,86 à
  +908,37 ppm. Aucun des 55 échantillons par pont à la borne ±2000 ppm ; aucune
  observation forcée de backlog. Cela n'exclut pas une excursion entre deux relevés.
- Corrections finales +400,84 / −400,23 ppm.
- Amorçage conservé : 12096 frames de silence natif rendu ; 8192 frames de silence
  côté graphe capture ; une discontinuité capture au démarrage. Aucun accroissement.

SHA-256 des exécutables testés :

| Composant (chemin relatif au dépôt) | SHA-256 |
|---|---|
| `build_codex_110/Release/TimoxVirtualAsioEngine.exe` | `FEB9504C0AEF7A89DA39BF5C7B5BD66EBD742418DCB5936FFF70B34D0A55F694` |
| `build_codex_110/Release/AsioEngineSignalProbe.exe` | `A553A1702040DB18EA5D7CFBCDF68848ED4FAD792502686C083E45E0E9AF1999` |
| `build_driver_110/Release/TimoxVasio.dll` | `F4FC8AA0EDB929B74DB2459D6B69652955A1BE9FB21370C6A109DDB96983A0B6` |

Le moteur installé API 1.0 est restauré, running 48 kHz / 2048, sans erreur ;
14 routes actives et 22 configurées avant et après. Le hash du fichier utilisateur
est inchangé. Mixxx, Traktor et SuperCollider sont restés présents : cet essai
prouve la présence du signal et la stabilité observée, pas la parité CPU/latence,
la transparence bit à bit, ni une stabilité prolongée. Un nouveau test long reste
nécessaire pour comparer à la session précédente de dix minutes.
