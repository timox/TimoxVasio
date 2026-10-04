# TimoxVasio — vérification ASIO

## Conclusion

Le code actif implémente l’interface `IASIO` du SDK et ses principales opérations de découverte, de format, d’allocation et de callbacks. Les probes locaux vérifient notamment 256 canaux par direction, l’index 255, le rejet de l’index 256 et le transport audio sur les canaux 0, 127 et 255.

Cela établit une conformité d’interface et des comportements testés; cela ne constitue pas une certification Steinberg ni une validation avec tous les hôtes ASIO. La validation du parcours complet jusqu’à une sortie matérielle reste à effectuer. La référence de l’interface est le fichier `asiosdk/common/iasiodrv.h` du SDK inclus au dépôt et la [définition IASIO du SDK](https://github.com/audiosdk/asio/blob/main/common/iasiodrv.h).

## Éléments vérifiés

| Sujet ASIO | Comportement du code actif | Preuve locale | État |
|---|---|---|---|
| Interface de pilote | `VASIODriver` implémente les méthodes abstraites de `IASIO` | [vasio_com_driver.h](src/vasio_com_driver.h) compile contre l’en-tête IASIO du SDK | Vérifié au build |
| Découverte et nom | Le pilote se nomme `TimoxVasio`; classe COM et fabrique sont enregistrées dans la DLL | [vasio_driver_factory.cpp](src/vasio_driver_factory.cpp), [vasio_com_driver.h](src/vasio_com_driver.h), probe COM | Vérifié par probe |
| Comptes de canaux | `getChannels` annonce 256 entrées et 256 sorties | [driver_probe.cpp](tests/driver_probe.cpp), [driver_audio_probe.cpp](tests/driver_audio_probe.cpp) | Vérifié |
| Index de canal | `getChannelInfo` accepte 0–255 et rejette 256; allocation sparse accepte 0, 127 et 255 | [vasio_driver.cpp](src/vasio_driver.cpp), probes du pilote | Vérifié |
| Formats de buffer | Les buffers sont float32 little-endian, double-bufferés; les indices et doublons sont contrôlés | `getChannelInfo` et `createBuffers` dans [vasio_driver.cpp](src/vasio_driver.cpp) | Vérifié par code et probe audio |
| Taille de bloc et fréquence | Sans moteur attaché, capacités annoncées selon le pilote; moteur attaché, seules les valeurs physiques effectives sont acceptées | `getBufferSize`, `canSampleRate`, `setSampleRate`, `createBuffers` dans [vasio_driver.cpp](src/vasio_driver.cpp) | Vérifié par code; hôte tiers à confirmer |
| Callbacks et position | `start`/`stop` pilotent un worker; les callbacks de buffer et la position échantillon/horodatage sont publiés | `callbackLoop` dans [vasio_driver.cpp](src/vasio_driver.cpp) et [driver_audio_probe.cpp](tests/driver_audio_probe.cpp) | Vérifié par probe transport |
| Contrôle optionnel | `controlPanel`, `future`, `outputReady` et sélection explicite d’horloge ne sont pas fournis (`ASE_NotPresent`) | [vasio_driver.cpp](src/vasio_driver.cpp) | Limitation déclarée |

## Capacité annoncée et canaux utilisés

ASIO sépare le nombre de canaux annoncé par `getChannels` des canaux pour lesquels l’hôte demande des buffers dans `createBuffers`. TimoxVasio annonce une capacité maximale de 256 par direction; le moteur ne publie que les canaux effectivement alloués par chaque client. Le nombre annoncé n’est donc pas une promesse que l’application ouvre tous les canaux.

Cette distinction est importante pour les hôtes qui stockent leur compte de canaux dans un entier trop petit pour représenter 256. La limitation de type observée dans certaines versions de Mixxx est documentée séparément dans [compatibilité Mixxx](docs/mixxx-256-channel-compatibility.md); elle ne change pas le contrat du pilote.

## À valider sur hôte et matériel

- Ouvrir le pilote dans un hôte tiers et confirmer le taux et la taille de bloc présentés après attachement au moteur.
- Vérifier les allocations sparse et l’index 255 depuis cet hôte, au-delà du probe COM local.
- Appliquer les routes depuis l’API/GUI, lire un signal connu et mesurer le signal correspondant sur l’entrée ou la sortie physique attendue.
- Vérifier stabilité, latence observée et transitions d’arrêt/reconfiguration avec les hôtes réellement visés.

Les instructions reproductibles de build et de sonde se trouvent dans [BUILD_DRIVERS.md](BUILD_DRIVERS.md). L’état global simplifié est dans le [README](README.md).
