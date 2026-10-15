# Legacy

Fichiers du prototype initial (PortAudio, projet Visual Studio `ASIOHost`). Ils ne sont ni compilés ni maintenus ; la compilation courante passe par `CMakeLists.txt` à la racine. Conservés pour l'historique.

## Scripts de test liés à la 1.1.1

`tests/` et `gui/` contiennent des scripts et une configuration de packaging codés sur la DLL et le moteur 1.1.1 (`registered-driver-smoke.ps1`, `switch_test_driver_registration.ps1`, `write_test_build_manifest.ps1`, `electron-builder.test.json`). Chacun porte le bandeau **INCOMPATIBLE SAUF 1.1.1** et échoue sur une installation 1.2.1.
