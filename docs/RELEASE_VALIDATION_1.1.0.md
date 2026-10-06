# Conditions de validation de TimoxVasio 1.1.0

**Français** | [English](RELEASE_VALIDATION_1.1.0.en.md)

Une compilation seule ne suffit pas à établir que l’application fonctionne. La validation doit relier les sources du commit aux binaires installés, puis vérifier le parcours réel.

## Provenance des trois composants

- Pilote ASIO : `build_driver_110/Release/TimoxVasio.dll`.
- Moteur audio et API : `build_codex_110/Release/TimoxVirtualAsioEngine.exe`.
- Interface : manifeste et sources sous `gui/`.

Le moteur dans le Setup/portable doit avoir le même SHA-256 que le moteur compilé. La DLL dans l’archive pilote doit avoir le même SHA-256 que le pilote compilé. `npm run dist` doit s’arrêter si une entrée ou une ressource manque ou diffère, puis produire un manifeste de validation indiquant commit, versions, chemins et empreintes.

## Validation logicielle

Pour chaque même commit : compiler le moteur, les tests natifs et la DLL; passer les tests de compatibilité du pilote, les tests du runtime audio, de l’API et des diagnostics; passer les tests de contrat, Electron et React; construire l’interface, le Setup et le portable; contrôler `git diff --check`. Aucun résultat d’un ancien build ne vaut pour un autre commit.

## Validation sur Windows et matériel

Installer ou lancer les artefacts dont les empreintes ont été contrôlées et vérifier :

1. l’interface `Timox VASIO Control` affiche l’état réel du moteur;
2. l’API locale et Swagger répondent depuis l’interface;
3. `engine.start` et `engine.stop` pilotent le flux sans arrêter l’hôte API;
4. les vumètres n’apparaissent que sur les canaux routés actifs et suivent le signal;
5. la corrélation L/R est proche de `+1` pour des canaux identiques, proche de `−1` après inversion de polarité, et signale `no_signal` en silence;
6. le ZIP installe séparément la DLL, qui est découverte et fonctionne avec l’hôte et le pilote ASIO physique testés;
7. les journaux enregistrent les changements et erreurs sans écriture depuis le callback audio.

Consigner Windows, versions des pilotes ASIO, hôte, fréquence, tampon et résultats audio. La conclusion vaut pour cette configuration vérifiée, pas pour chaque combinaison de matériel et de pilotes.
