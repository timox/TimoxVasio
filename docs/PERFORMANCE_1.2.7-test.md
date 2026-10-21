# Évaluation du gain global — 1.2.7-test

Mesure locale du 21 octobre 2026, Release x64, Windows, 48 kHz.

## Résultat et portée

Le **parcours de données audio testé** demande 41 à 64 % de temps en moins.
Cela comprend les conversions d'entrée/sortie, les vrais anneaux partagés,
le routage, les mètres de crête, la signalisation et les copies planaire/entrelacé.
Ce n'est **pas** une baisse de 41 à 64 % de l'utilisation CPU totale du PC :
le DSP des applications, leurs plugins, l'interface, le polling de contrôle,
le pilote physique et l'ordonnancement interprocessus ne sont pas mesurés ici.

M1 vise la régularité sous charge, pas une économie CPU démontrée. M2 peut réduire
la latence de sortie si la carte le permet ; ce gain matériel n'a pas été mesuré.
La comparaison ci-dessous isole le gain M3 sur le parcours logiciel plus large.

## Méthode reproductible

`AudioPathBenchmark`, source `tests/audio_path_benchmark.cpp`, utilise le même exécutable
pour alterner les conversions scalaires antérieures (inchangées) et les conversions par blocs.
Le vrai AudioRoutingRuntime et AudioClientMapping traitent des routes bidirectionnelles
à gain −3 dB avec un client synthétique, DSP identité. Les autres opérations sont identiques.
Les buffers PCM résultants sont comparés entre les deux parcours.

Deux échauffements puis sept répétitions, ordre scalaire/bloc alterné, médiane des temps
par bloc. Chaque répétition contient 2000 blocs de 256 trames ou 300 blocs de 2048 trames.
Une identité de mapping privée empêche le moteur actif de découvrir les anneaux du test ;
les premiers essais non isolés, interrompus par cette découverte, ont été rejetés.
Les applications audio de l'utilisateur continuent de tourner : il ne s'agit pas d'un PC au repos.
Les deux parcours s'exécutent séquentiellement sur un thread, sans attente d'une carte réelle.

Commandes :

```powershell
git rev-parse --show-toplevel
git status --short
cmake --build build_codex_110 --config Release --target AudioPathBenchmark
./build_codex_110/Release/AudioPathBenchmark.exe
```

## Résultats

| Trames | Canaux entrée/sortie | Format LSB | Avant (µs/bloc) | Après (µs/bloc) | Temps économisé |
|---:|---:|---|---:|---:|---:|
| 256 | 2 / 2 | Int24 | 61,467 | 25,406 | 58,7 % |
| 256 | 2 / 2 | Int32 | 60,019 | 21,513 | 64,2 % |
| 256 | 2 / 2 | Float32 | 37,063 | 19,235 | 48,1 % |
| 256 | 8 / 8 | Int24 | 268,074 | 117,415 | 56,2 % |
| 256 | 8 / 8 | Int32 | 297,255 | 110,158 | 62,9 % |
| 256 | 8 / 8 | Float32 | 162,665 | 87,337 | 46,3 % |
| 2048 | 2 / 2 | Int24 | 650,340 | 232,393 | 64,3 % |
| 2048 | 2 / 2 | Int32 | 552,871 | 218,301 | 60,5 % |
| 2048 | 2 / 2 | Float32 | 317,793 | 176,843 | 44,4 % |
| 2048 | 8 / 8 | Int24 | 2430,024 | 1130,556 | 53,5 % |
| 2048 | 8 / 8 | Int32 | 2783,553 | 1027,669 | 63,1 % |
| 2048 | 8 / 8 | Float32 | 1670,634 | 991,260 | 40,7 % |

Exemple : à 256 trames, la période disponible est 5333 µs. En stéréo Int32,
la part de cette période occupée par le test passe de 1,125 % à 0,403 %.
C'est environ **0,72 point du budget d'un cœur** gagné, pas 64 points de CPU système.
À 8 canaux Int32 / 2048 trames, elle passe de 6,524 % à 2,409 %.
Il s'agit de durées écoulées mesurées, pas de compteurs CPU matériels par processus.

## Observation de la session réelle, avant installation

Le moteur installé est toujours celui de la stable :
`C:/Program Files/Timox VASIO Control/resources/backend/TimoxVirtualAsioEngine.exe`,
UTC `2026-10-18T22:05:36Z`, SHA-256
`39144511775B9FAACCE817DD5462E62F13886758AA92C410C330450B42D1FB44`.

Six relevés espacés de cinq secondes à 06:05:40–06:06:06 UTC : SSL ASIO Driver 1,
48 kHz, 2048 trames, 14 routes actives. outputCopy moyen : 0,410–0,618 ms/bloc ;
callback physique maximal par fenêtre : 2,935–4,630 ms ; aucun retard ni dépassement
dans ces fenêtres. La compilation en parallèle et l'activité des applications font varier
la charge : ces relevés ne constituent pas une comparaison A/B de la session réelle.

Une réduction hypothétique de 70 % de la seule copie de sortie économiserait ici
0,29–0,43 ms sur une période de 42,67 ms, soit environ 0,7–1,0 point du budget d'un cœur.
C'est une **estimation**, pas un résultat après installation ; le format de sortie et
le coût propre à outputReady doivent encore être confirmés sur la carte.

La charge globale avec les plugins réels et le gain de latence nécessitent encore une
mesure avant/après installation, avec exactement les mêmes projets et conditions de lecture.
Les relevés bruts locaux sont dans `tests/results/` (ignoré par Git).
