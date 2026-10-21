# Moteur commun ASIO/WASAPI

**Français** | [English](2026-10-21-unified-audio-engine-design.en.md)

Objectif confirmé : un équivalent fonctionnel de PipeWire sous Windows pour relier applications et périphériques ASIO/WASAPI. Le mainteneur a demandé de réorganiser le plan puis de commencer.

## Responsabilités

TimoxVasio.dll reste le pilote ASIO virtuel : interface hôte, profils, buffers et transport existant. Il n'ouvre pas de périphérique WASAPI et ne contient pas de SRC. Son indépendance concerne le choix des backends ; il utilise toujours le moteur pour la cadence et le routage.

Le moteur possède le graphe, les routes, les formats, la politique de cadence et les backends. L'hôte ASIO conserve le mode actuel. Les backends WASAPI partagés fournissent capture/rendu/loopback sans carte ASIO.

Une cadence logicielle est le choix initial pour le routage virtuel et le partagé sans carte ASIO ; elle sera qualifiée avant livraison. Elle ne tourne jamais en concurrence avec une cadence matérielle sur le même graphe. Le changement de maître est explicite et coordonné. Les horloges des périphériques se raccordent par anneaux et SRC adaptatif hors callback.

Le loopback ne crée pas un microphone virtuel. Un câble virtuel tiers fournit les endpoints Windows au premier stade ; des endpoints natifs constituent un lot ultérieur distinct. WASAPI exclusif reste optionnel.

## Acceptation produit

- ASIO→ASIO sans carte physique.
- Source Windows→application ASIO.
- ASIO→périphérique WASAPI partagé.
- Graphe→microphone sélectionnable par une application Windows via câble tiers.
- Routes conservées lorsque des applications ou périphériques disparaissent.

## Invariants

Exigence ajoutée par le mainteneur : contrôle graphique, couverture API et performances au moins équivalents au TimoxVasio actuel. Control devient l'interface unifiée ; aucune opération nécessaire ne doit rester réservée à une sonde ou au code. Préserver les outils de routage, profils, diagnostic, mesures et édition transactionnelle existants. Qualifier sur le même poste/charge le chemin ASIO de référence puis les nouveaux chemins, avec distributions callback, charge CPU, latence bout en bout, incidents et coût de conversion. Ne pas confondre coût ajouté d'un pont avec une régression du chemin ASIO sans pont.

Mode ASIO existant conservé. Aucun travail bloquant ajouté au callback. Format/mémoire immuables par génération, retraite avant libération, aucun reset concurrent d'anneau. Validation avant publication et révision transactionnelle. API seule interface de contrôle. UI conforme à AGENTS.md.

## Premier incrément

Découpler AudioController de PhysicalAsioHost via AudioClockHost. Types neutres avec aliases compatibles. Fabrique injectable sur le worker, défaut ASIO inchangé. Tester négociation et lifecycle sans matériel ; garder suites runtime pour les données audio. Aucun changement public JSON ou ABI transport.

Ajouter une vérification de frontière des sources/dépendances de la DLL, avec fixtures négatives. Cet audit statique n'est pas un interpréteur CMake complet ni une preuve contre le chargement dynamique.

## Au-delà du premier incrément

Cadence logicielle, backends WASAPI, contrat d'horloge API, interface de ponts et qualification sont planifiés ensuite. Aucune installation, signature ou distribution implicite.
