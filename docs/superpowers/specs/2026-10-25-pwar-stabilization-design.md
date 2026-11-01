# Stabilisation PWAR pour l'évolution de Timox

L'utilisateur autorise la consolidation de PWAR, y compris les changements de
structure nécessaires, et retient l'évolution de Timox plutôt qu'un portage de
PipeWire sous Windows. Une PR PWAR sera préparée après qualification réelle.
Les objectifs du fichier joint restent tous requis : duplex, carte ASIO locale
en parallèle, fréquences/quanta, supervision/API et contrôle distant authentifié.

## Preuve qui motive le changement Linux

Le candidat a été démarré dans le graphe réel. GDB, au point d'entrée du callback,
observe `position->clock.duration=1024`, `rate={1,44100}`, malgré la demande CLI
128/48000. Le correctif de protection 0007 produit alors du silence. Un paquet
UDP de diagnostic Linux déclenche bien un callback du véritable hôte ASIO Timox
Windows : le réseau n'est pas la cause de l'absence d'émission du relais.

## Choix d'implémentation

Employer deux `pw_stream` négociant du float32 stéréo à la fréquence réseau,
avec les adaptateurs de PipeWire pour la conversion avec le graphe réel.
Ne pas forcer la fréquence ni le quantum du graphe global. Les deux nœuds sont
reliables aux applications Linux par les outils PipeWire ordinaires.

Le callback capture écrit des frames dans une file SPSC préallouée ; le callback
lecture lit dans une seconde file et complète de silence ce qui manque. Le
thread réseau, propriétaire des sockets, découpe les frames en paquets PWAR,
valide et réassemble les réponses, puis alimente la file de lecture. Les callbacks
ne prennent aucun mutex, n'attendent pas le réseau et ne font pas de journalisation.

Chaque file a exactement un producteur et un consommateur ; aucun reset en
activité. Les dimensions sont bornées avant toute copie. Les pertes et le silence
sont comptés, pas masqués. L'arrêt désactive les flux, attend le thread réseau,
puis libère les buffers hors du thread audio. Les erreurs de démarrage reviennent
au contrôleur, sans `exit()` dans la bibliothèque.

Le format UDP historique est conservé pour la première qualification, avec ses
limites explicites de fréquence convenue et d'absence d'identifiant de session.
Le format négocié doit être le même côté ASIO. La reconnexion, la détection des
redémarrages et l'adaptation à la carte ASIO indépendante restent des exigences
de livraison, même si elles ne sont pas toutes réalisées dans cette étape.

## Ordre et preuves attendues

1. File SPSC : exactitude stéréo, quanta variables, dépassement/sous-alimentation,
   bouclage des indices et stress réel à deux threads sous sanitizers.
2. Backend flux : négociation 48 kHz sur le graphe observé à 44,1 kHz, absence
   d'accès hors bornes, création/arrêt/recréation et diagnostic de conflit de port.
3. Aller-retour mesuré : source Linux, hôte Timox avec DLL PWAR réelle, traitement
   connu et capture du retour Linux. Contrôler amplitude, fréquence, ordre des
   canaux et continuité. Une absence de son est un échec, même avec des ports reliés.
4. Pertes, coupure/reprise, changement de quantum/fréquence et essai prolongé.
5. Intégration de la liaison comme ports du graphe Timox indépendants de sa carte
   ASIO, puis état/API et accès distant authentifié.

Le nouveau backend doit remplacer les chemins dangereux après qualification ;
son existence ou sa compilation seule ne valide ni PWAR ni l'objectif global.
