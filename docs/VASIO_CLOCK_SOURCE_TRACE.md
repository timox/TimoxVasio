# Source de cadence de TimoxVasio

L'ASIO virtuel est cadencé par les callbacks du pilote ASIO physique sélectionné dans le moteur. Ce constat vient du code, indépendamment des mesures de dérive encore à effectuer.

1. `AudioController::ApplyConfigurationCore` exige un pilote physique pour configurer fréquence, buffer et routes (`src/audio_controller.cpp:558-564`), confirme fréquence et taille avec ce pilote (`:575-608`) et démarre son hôte quand des routes sont actives (`:627-644`). Sans route résolue, l'état reste `stopped` et aucun callback physique n'est démarré.
2. Le callback du pilote physique appelle `PhysicalAsioHost::Session::Process` (`src/physical_asio_host.cpp:23-30,134-163`). Celui-ci appelle le traitement de routage, qui écrit le bloc d'entrée du client virtuel et signale son événement (`src/audio_routing_runtime.cpp:361-382`).
3. Le thread callback de la DLL TimoxVasio attend cet événement, lit les échantillons disponibles, appelle `bufferSwitchTimeInfo` ou `bufferSwitch`, puis avance sa position d'échantillon d'une taille de bloc (`src/vasio_driver.cpp:349-399`). Il n'y a pas de minuterie autonome dans cette boucle.

`QueryPerformanceCounter` fournit des horodatages et des mesures de durée (`src/vasio_driver.cpp:22-30,382`; `src/physical_asio_host.cpp:136-139,177-188`) ; il ne fixe pas la cadence des callbacks virtuels. La source ultime du rythme dépend de la carte et du réglage d'horloge du pilote physique : quartz interne, entrée numérique externe ou autre. Le doctor peut donc nommer le pilote physique qui cadence TimoxVasio, mais doit conserver « indéterminée » pour l'oscillateur tant que les déclarations, l'état de verrouillage et la comparaison avec une seconde carte indépendante ne permettent pas de conclure.

Pour le mode 7, la référence doit être indépendante du pilote physique utilisé par le moteur. Deux noms ASIO qui ouvrent la même SSL ne constituent pas deux horloges à comparer ; ASIO4ALL doit sélectionner le casque USB distinct annoncé par l'utilisateur.
