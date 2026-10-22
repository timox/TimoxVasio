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

Ces résultats ne qualifient pas encore la correction sur matériel. Ils ne
qualifient pas non plus les petits quanta : le dimensionnement FIFO historique
peut être inférieur à un paquet natif converti et fait l'objet d'un lot séparé.
La DLL ASIO reste inchangée.
