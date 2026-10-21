# Livrables Windows 1.2.7-test

Préversion : [v1.2.7-test](https://github.com/timox/TimoxVasio/releases/tag/v1.2.7-test).
La stable 1.2.6 reste la version recommandée hors tests.

- `Timox VASIO Control Setup 1.2.7-test.exe` : interface, moteur et installation du pilote.
- `Timox VASIO Control 1.2.7-test.exe` : portable, sans installation du pilote.
- `TimoxVasio Driver 1.2.7-test.zip` : pilote et scripts d'installation manuelle.
- `MANIFEST.json` : commit source, horodatages et empreintes SHA-256.

Les pré/postflight contrôlent le moteur canonique, le pilote canonique, le pilote dans
le ZIP et le Setup, et le moteur extrait de **chacun** des deux exécutables Electron.
Les fichiers sont construits depuis `build_driver_110/Release` et `build_codex_110/Release`.

Tests natifs de conversion/routage réussis après changement de version. Les sept scénarios
DriverAudioProbe avaient réussi après M1. Au passage de publication, six réussissent et
`--reinit-after-detach` échoue deux fois dans la préparation commune, avant d'exercer la
réinitialisation (message générique, pas celui de cette branche). Trois répétitions
suivantes sur la DLL test réussissent. La stable est testée en alternance ; aucun correctif
n'est déduit de cette intermittence. Une validation sans moteur concurrent reste requise.
Échec DriverProbe préexistant sur taille fixe documenté.
Les [notes](RELEASE_NOTES_v1.2.7-test.md), le [rapport de vérification](plans/2026-10-20-optimisation-chemin-audio-rapport.md)
et la [mesure de performance](PERFORMANCE_1.2.7-test.md) détaillent les limites des tests.

L'installation locale et la réouverture des applications audio restent à effectuer
par l'utilisateur ; la publication ne remplace pas les binaires de sa session active.
