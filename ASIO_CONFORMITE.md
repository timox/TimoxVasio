# TimoxVasio — vérification ASIO

**Français** | [English](ASIO_CONFORMITE.en.md)

## Conclusion

Le code actif implémente l’interface `IASIO` du SDK et ses principales opérations de découverte, de format, d’allocation et de callbacks. Les probes locaux vérifient notamment 256 canaux par direction, l’index 255, le rejet de l’index 256 et le transport audio sur les canaux 0, 127 et 255.

Cela établit une conformité d’interface et des comportements testés; cela ne constitue pas une certification Steinberg ni une validation avec tous les hôtes ASIO. Le test de bout en bout réalisé sur ce poste et ses relevés sont détaillés dans la section « Validation hôte et matériel » ci-dessous. La référence de l’interface est le fichier `asiosdk/common/iasiodrv.h` du SDK inclus au dépôt et la [définition IASIO du SDK](https://github.com/audiosdk/asio/blob/main/common/iasiodrv.h).

## Éléments vérifiés

| Sujet ASIO | Comportement du code actif | Preuve locale | État |
|---|---|---|---|
| Interface de pilote | `VASIODriver` implémente les méthodes abstraites de `IASIO` | [vasio_com_driver.h](src/vasio_com_driver.h) compile contre l’en-tête IASIO du SDK | Vérifié au build |
| Découverte et nom | Le pilote se nomme `TimoxVasio`; classe COM et fabrique sont enregistrées dans la DLL | [vasio_driver_factory.cpp](src/vasio_driver_factory.cpp), [vasio_com_driver.h](src/vasio_com_driver.h), probe COM | Vérifié par probe |
| Comptes de canaux | `getChannels` annonce 256 entrées et 256 sorties | [driver_probe.cpp](tests/driver_probe.cpp), [driver_audio_probe.cpp](tests/driver_audio_probe.cpp) | Vérifié |
| Index de canal | `getChannelInfo` accepte 0–255 et rejette 256; allocation sparse accepte 0, 127 et 255 | [vasio_driver.cpp](src/vasio_driver.cpp), probes du pilote | Vérifié |
| Formats de buffer | Les buffers sont float32 little-endian, double-bufferés; les indices et doublons sont contrôlés | `getChannelInfo` et `createBuffers` dans [vasio_driver.cpp](src/vasio_driver.cpp) | Vérifié par code et probe audio |
| Taille de bloc et fréquence | Sans moteur attaché, capacités annoncées selon le pilote; moteur attaché, seules les valeurs physiques effectives sont acceptées | `getBufferSize`, `canSampleRate`, `setSampleRate`, `createBuffers` dans [vasio_driver.cpp](src/vasio_driver.cpp); test Renoise du 5 octobre | Vérifié sur Renoise à 48 kHz/1024 frames; couverture d’autres hôtes complémentaire |
| Callbacks et position | `start`/`stop` pilotent un worker; les callbacks de buffer et la position échantillon/horodatage sont publiés | `callbackLoop` dans [vasio_driver.cpp](src/vasio_driver.cpp) et [driver_audio_probe.cpp](tests/driver_audio_probe.cpp) | Vérifié par probe transport |
| Contrôle optionnel | `controlPanel`, `future`, `outputReady` et sélection explicite d’horloge ne sont pas fournis (`ASE_NotPresent`) | [vasio_driver.cpp](src/vasio_driver.cpp) | Limitation déclarée |

## Capacité annoncée et canaux utilisés

ASIO sépare le nombre de canaux annoncé par `getChannels` des canaux pour lesquels l’hôte demande des buffers dans `createBuffers`. TimoxVasio annonce une capacité maximale de 256 par direction; le moteur ne publie que les canaux effectivement alloués par chaque client. Le nombre annoncé n’est donc pas une promesse que l’application ouvre tous les canaux.

Cette distinction est importante pour les hôtes qui stockent leur compte de canaux dans un entier trop petit pour représenter 256. La limitation de type observée dans certaines versions de Mixxx est documentée séparément dans [compatibilité Mixxx](docs/mixxx-256-channel-compatibility.md); elle ne change pas le contrat du pilote.

## Validation hôte et matériel — 5 octobre 2026

Après réinstallation de la version 1.0.0, l’utilisateur a confirmé le test
audible de bout en bout avec Renoise et SSL ASIO Driver 1. L’API a relevé le
moteur `running` à 48 kHz/1024 frames, un client Renoise avec 64 entrées et
64 sorties, et huit routes actives. Le flux `audio.meter` a fourni 60 événements
sur 3,5 secondes pour les sorties physiques routées et les sorties virtuelles
sources; les crêtes observées étaient comprises entre `-101,65` et `-26,43 dBFS`.
Les diagnostics consignent l’application des huit routes et la configuration
physique. Swagger a également été vérifié par l’utilisateur dans Electron.

Cette validation concerne le circuit testé sur ce poste. La couverture d’autres
hôtes ASIO, périphériques et transitions peut être étendue séparément.

Les instructions reproductibles de build et de sonde se trouvent dans [BUILD_DRIVERS.md](BUILD_DRIVERS.md). L’état global simplifié est dans le [README](README.md).
