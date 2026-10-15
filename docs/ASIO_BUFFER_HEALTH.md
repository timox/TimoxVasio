# Santé du tampon ASIO physique

**Français** | [English](ASIO_BUFFER_HEALTH.en.md)

Timox VASIO Control affiche la dernière observation de cinq secondes du callback ASIO physique dans **Performance → Physical ASIO buffer**. Les mêmes valeurs sont disponibles sous `bufferHealth` dans `GET /api/v1/state`.

La période d'un bloc vaut `bufferFrames / sampleRate`. Le moteur mesure le temps de traitement autour de la conversion d'échantillons et du routage, compte les callbacks plus longs qu'un bloc, et compte les démarrages séparés de plus de 1,5 période de bloc. La pire marge est la période du bloc moins l'appel de traitement le plus long de la fenêtre. Une marge négative signifie qu'au moins un callback a dépassé sa période nominale.

L'indicateur dit **insufficient** quand une fenêtre compte moins de 100 callbacks ou moins d'une seconde d'observation. Il dit **warning** après trois fenêtres consécutives suffisamment remplies avec au moins 1 % de démarrages retardés ou de dépassements de traitement, avec un minimum de trois incidents par fenêtre. Il revient à **healthy** après trois fenêtres suffisamment remplies sous ce seuil. Une lecture saine signifie que cette règle n'a pas trouvé de pression temporelle répétée ; elle ne garantit pas un son sans craquement.

`bufferHealth` vaut `null` avant la première fenêtre complète et après un arrêt ou une reconfiguration. Le moteur ne change jamais la taille du tampon automatiquement. La mesure du callback ne peut pas identifier toutes les défaillances de pilote ou de matériel, et un callback retardé ne prouve pas à lui seul une interruption audible. Comparer les compteurs affichés avec le son réel et, en essayant un tampon plus grand, garder une fréquence et une charge comparables. Les pertes du transport des clients virtuels sont rapportées séparément par `audio.meter`.

L'onglet Performance schématise le trajet audio et le temps observé du callback physique dans une période de tampon. Le schéma ne mesure pas la latence totale de l'application aux enceintes et ne recommande aucune autre taille de tampon.

Le schéma d'horloge suit le périphérique ASIO physique sélectionné : ses callbacks cadencent TimoxVasio, qui réveille les clients ASIO virtuels. Cela évite qu'une horloge virtuelle séparée dérive par rapport à ce périphérique ; l'ordonnancement des callbacks peut toujours avoir de la gigue, et un autre périphérique physique peut dériver.

Le schéma de transport montre qu'une application écrit un bloc de sortie complet en mémoire partagée et que le moteur le lit lors d'un callback physique suivant. Une route passant par une autre application ASIO ajoute une remise supplémentaire. La durée du bloc est calculée à partir du nombre de trames et de la fréquence sélectionnés ; ce n'est pas une mesure de latence de bout en bout.

La section **Virtual transport** affiche les compteurs existants `underruns` et `overruns` de `audio.meter` pour les directions des clients utilisées par des routes actives. Ces compteurs sont cumulés sur la durée de vie de chaque transport client. Un callback client terminé ne prouve pas qu'il transportait un signal audible. L'interface ne demande aucune mesure par bloc et n'inspecte aucun échantillon pour remplir cette section ; elle n'ajoute donc aucun travail au callback audio. La latence réelle de l'application aux enceintes exige une mesure séparée de bout en bout, conversion matérielle comprise.
