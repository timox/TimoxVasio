# TimoxVasio 1.2.7-test

Préversion de test des optimisations audio. La version stable reste 1.2.6.

## Ce qui change, simplement

- **Priorité audio** : Windows donne davantage de priorité au thread chargé du son,
  pour réduire le risque de retard lorsque le PC est occupé.
- **Sortie prête** : le moteur avertit la carte dès que les échantillons sont prêts.
  Les pilotes compatibles peuvent ainsi réduire leur délai de sortie.
- **Conversions par blocs** : la méthode de conversion est choisie une fois par canal,
  puis appliquée à tout le bloc de son. Cela évite du travail répété pour chaque échantillon.

Le contenu audio reste identique : les tests comparent les résultats bit pour bit sur
18 formats, avec un balayage exhaustif 16 bits et des millions de cas aléatoires.
Le benchmark local de conversion seule montre environ 3,5 à 4,2 fois moins de temps sur
la dernière exécution ; ce n'est pas une mesure du gain global du moteur.

Un [benchmark du parcours audio complet](PERFORMANCE_1.2.7-test.md) mesure **41 à 64 % de
temps en moins** pour transport + routage + conversions, hors DSP des applications et
pilote matériel. Ce pourcentage ne représente pas la charge CPU totale du PC.

## Installation

Utiliser **Timox VASIO Control Setup 1.2.7-test.exe** pour installer l'interface, le moteur
et le pilote. Le pilote est installé dans
`C:\Program Files\Steinberg\VirtualASIO\1.2.7-test\TimoxVasio.dll`.
Relancer les applications audio pour qu'elles chargent la nouvelle DLL.

La version portable inclut le moteur mais n'installe pas le pilote. Le ZIP permet
l'installation manuelle. Pour revenir à la stable, réinstaller le Setup 1.2.6 et
relancer les applications audio.

## Vérifications et limites

Compilation Release x64 et tests de conversion, routage et transport réussis.
Les sept scénarios DriverAudioProbe passent. DriverProbe présente un échec préexistant
sur une taille de bloc fixe (granularité nulle). PhysicalAsioProbe vérifie l'inventaire,
pas le fonctionnement audio d'une carte.

Les gains de latence matérielle, la cadence sous charge et la coexistence avec les hôtes
réels restent à mesurer. M4 n'est pas incluse. L'interface, l'API et le format de transport
v9 sont conservés.

Voir le [rapport détaillé](plans/2026-10-20-optimisation-chemin-audio-rapport.md).
`MANIFEST.json` accompagne les livrables pour identifier le commit et les SHA-256.
