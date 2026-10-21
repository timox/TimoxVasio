# TimoxVasio 1.2.7

Version stable des optimisations audio validées avec la 1.2.7-test, avec un bouton de réorganisation du schéma.

## Nouveautés

- **Auto-arrange**, à côté des commandes de zoom : dispose les blocs visibles en colonnes, espace les blocs selon leur taille et recadre la vue. Les positions sont mémorisées, les noms personnalisés et les connexions audio sont conservés.
- Priorité Windows MMCSS « Pro Audio » du thread audio, avec repli si ce service est indisponible.
- Notification de sortie prête aux pilotes physiques qui la prennent en charge.
- Conversion audio par blocs, avec résultats vérifiés bit pour bit sur les formats pris en charge.

## Performances

Le [benchmark du parcours logiciel audio](PERFORMANCE_1.2.7-test.md) mesure 41 à 64 % de temps en moins sur transport, routage et conversions.

Les relevés locaux de la session réelle à 48 kHz / 2048 échantillons montrent également des gains plausibles : copie de sortie de 0,501 à 0,122 ms par bloc, lecture des applications de 0,606 à 0,092 ms, routage de 0,152 à 0,068 ms. Aucun retard ni dépassement n'a été observé dans les fenêtres relevées. Ces mesures concernent ces étapes audio, pas la charge CPU totale du PC ; les conditions de charge n'étaient pas strictement identiques.

## Installation

Utiliser **Timox VASIO Control Setup 1.2.7.exe** pour installer l'interface, le moteur et le pilote. Le pilote est enregistré dans `C:\Program Files\Steinberg\VirtualASIO\1.2.7\TimoxVasio.dll`. Relancer les applications audio après installation pour charger cette version.

Le portable inclut le moteur mais n'installe pas le pilote. Le ZIP contient le pilote et les scripts d'installation manuelle. `MANIFEST.json` identifie le commit source et les SHA-256 des livrables.

## Validation

Les tests de l'interface couvrent la réorganisation, la conservation des noms et l'absence de modification des connexions. Les tests natifs couvrent les conversions, le routage et les mesures audio. Le contenu du Setup, du portable et du ZIP est comparé aux binaires construits.

La préversion a également passé les essais locaux du pilote installé : cycles démarrage/arrêt, rechargement et cadence audio. Les limites des anciens outils de test restent documentées dans le [rapport de la préversion](plans/2026-10-20-optimisation-chemin-audio-rapport.md). La latence matérielle n'a pas été quantifiée.

L'API et le transport audio v9 restent compatibles. Les développements WASAPI en cours ne font pas partie de cette version.
