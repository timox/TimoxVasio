# Feuille de route

**Français** | [English](ROADMAP.en.md)

Travaux envisagés après la version 1.2.4. Aucun n'est daté ni engagé ; l'ordre ne reflète pas une priorité.

- **Valider la compatibilité avec ASIO4ALL.** ASIO4ALL peut être sélectionné comme pilote physique, mais aucune version de TimoxVasio n'a été validée avec lui. Tester l'ouverture, la reconfiguration et le routage avec un périphérique WDM derrière ASIO4ALL, et documenter les limites.
- **Corriger la dérive d'une horloge MIDI à partir de l'horloge de la carte son.** Utiliser la position d'échantillons du pilote physique, qui sert déjà d'horloge maître, comme référence de temps pour une horloge MIDI, afin que celle-ci ne dérive pas par rapport à l'audio.
- **Fiabiliser l'exemple SuperCollider et ses tests audio.** Diagnostiquer les échecs observés lors des rendus WAV : un canal de la chaîne à six canaux peut être silencieux et le test du compresseur peut mesurer un niveau de sortie supérieur à celui attendu. Vérifier le routage des canaux, le gain du traitement et les assertions avant de considérer l'exemple comme validé.
- **Produire des graphiques temporels sans dégrader les performances.** Afficher dans Control l'évolution dans le temps des niveaux, de la corrélation et des mesures de performance, sans ajouter de travail dans le callback audio ni augmenter la cadence de publication de l'API.
- **Simplifier l'installateur Windows.** Réunir en une seule étape l'installation de Control, du moteur et de l'enregistrement du pilote ASIO, qui passent aujourd'hui par deux livrables et un script à lancer en administrateur.
- **Décliner TimoxVasio pour Linux en s'appuyant sur PipeWire.** Étudier une version où PipeWire remplace le pilote ASIO virtuel et la carte physique, en conservant le moteur de routage, l'API locale et Control.
- **Faciliter les retours de test par un dump à transmettre.** Produire en un geste, depuis Control ou l'API, une archive de diagnostic (versions et empreintes de la DLL et du moteur, configuration, profils, routes, état, dernières mesures de performance, journaux) que l'utilisateur peut joindre à un rapport de bug, sans données audio ni informations personnelles inutiles.
- **Relier les applications ASIO à l'audio Windows (WASAPI).** Ajouter au moteur des ponts WASAPI (capture, loopback de périphérique ou de processus, rendu) synchronisés sur l'horloge de la carte ASIO, puis un mode sans carte ASIO où un périphérique WASAPI exclusif fournit l'horloge. La conception est décrite dans [WASAPI_BRIDGE_SPEC.md](WASAPI_BRIDGE_SPEC.md).
