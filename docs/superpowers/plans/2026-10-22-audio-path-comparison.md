# Comparaison du chemin ASIO après intégration WASAPI

## Portée

Comparaison locale du 22 octobre 2026, référence source `f8984be824197c326d2ea88fdcd5be5949282c8f`, sources courantes `78b0278`. Même `tests/audio_path_benchmark.cpp`, mêmes sources de conversion PCM et de transport (diff Git vide). Le runtime et son graphe incorporent les ajouts WASAPI dans la variante courante. Aucun pont WASAPI actif dans ce benchmark.

Ce test utilise un mapping de client synthétique, pas une application ASIO réelle. Il mesure le coût d'un aller-retour de traitement avec conversions, graphe et copies. Il ne mesure ni latence acoustique, ni ordonnancement réel du pilote, ni coût des workers SRC WASAPI.

## Construction et protocole

Sources de référence extraites par `git archive f8984be` (`src`, `include`, `tests/audio_path_benchmark.cpp`, `asiosdk/common`) sous `build_wasapi_baseline/reference-f8984be`. Deux cibles identiques CMake C++17, générateur `Visual Studio 18 2026`, architecture x64, configuration Release, lien `ole32` : `AudioPath-reference` et `AudioPath-current`. Sources de chaque cible : benchmark, audio_routing_runtime.cpp, routing_graph.cpp, audio_transport.cpp et asio_sample_conversion.cpp ; includes de sa propre variante `include`, `src`, `asiosdk/common`.

Quatre exécutions séquentielles dans l'ordre référence, courant, courant, référence ; toutes code 0. Chaque valeur ci-dessous est le champ `after_us` : médiane de sept moyennes de traitement par bloc, après chauffe. Les champs historiques `before_us`/`after_us` du benchmark comparent traitement scalaire/bloc **à l'intérieur d'un binaire** ; ils ne désignent pas les révisions Git. Les résultats ne sont donc pas des percentiles de callbacks individuels.

Aucune compilation lancée pendant la fenêtre. Le moteur utilisateur et ses applications restent actifs ; activité système et navigateur non éliminée. Pas d'affinité CPU ni de verrouillage de fréquence imposés. Les sorties brutes locales sont `build_wasapi_baseline/run-1-reference.txt` à `run-4-reference.txt` (runs 2 et 3 : `current`).

| Frames | Canaux | PCM | Référence 1 µs | Courant 2 µs | Courant 3 µs | Référence 4 µs |
|---:|---:|---|---:|---:|---:|---:|
| 256 | 2 | int24 | 15.725 | 13.804 | 17.223 | 13.925 |
| 256 | 2 | int32 | 14.314 | 12.607 | 13.963 | 11.689 |
| 256 | 2 | float32 | 10.336 | 8.558 | 11.477 | 8.468 |
| 256 | 8 | int24 | 50.757 | 55.062 | 48.975 | 49.392 |
| 256 | 8 | int32 | 47.002 | 44.783 | 45.837 | 46.309 |
| 256 | 8 | float32 | 32.311 | 38.495 | 32.018 | 33.669 |
| 2048 | 2 | int24 | 95.903 | 103.341 | 102.520 | 122.887 |
| 2048 | 2 | int32 | 103.820 | 88.343 | 91.385 | 131.905 |
| 2048 | 2 | float32 | 75.069 | 61.365 | 61.813 | 89.155 |
| 2048 | 8 | int24 | 549.663 | 409.114 | 412.457 | 513.910 |
| 2048 | 8 | int32 | 411.324 | 373.729 | 514.696 | 375.203 |
| 2048 | 8 | float32 | 321.865 | 307.823 | 360.644 | 273.148 |

## Interprétation et suite

La hausse importante observée lors de la toute première comparaison avec un ancien binaire n'est pas reproduite uniformément. Certaines cellules augmentent et d'autres baissent ; les variations entre exécutions d'une même version restent importantes. **La parité de performances n'est pas démontrée**, et cette série ne justifie ni une optimisation arbitraire ni une déclaration d'absence de régression.

La qualification restante doit mesurer p50/p95/max de callbacks individuels sous charge contrôlée, puis le coût CPU des ponts actifs, les pertes et la latence par corrélation. Les budgets moyens faibles de ce microbenchmark ne prouvent pas le respect de toutes les deadlines.

SHA-256 des exécutables comparés :

- `build_wasapi_baseline/build/Release/AudioPath-reference.exe` : `6FEE7DC19BA63FB4262D9A32344CDD6310C796D84F50A1B936DDDE809A605EF6`.
- `build_wasapi_baseline/build/Release/AudioPath-current.exe` : `3CBF546DFBD79F17475643B6C3A1F67D40A630F6C45517D3A96472569BDAF83B`.
