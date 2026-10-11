# Build portable de test TimoxVasio

Le dossier `tests/results/test-electron/win-unpacked` contient **Timox VASIO
Control Test.exe** et le moteur de test v7. Il ne contient pas de pilote ASIO
enregistré et ne remplace pas l'application installée. Si une API TimoxVasio
fonctionne déjà sur le port 52525, l'interface se connecte à cette instance :
elle peut alors afficher le moteur installé plutôt que le moteur embarqué.

La DLL de test reste séparée dans `tests/results/test-driver-1.1.1-test.1.zip`.
Cette archive n'installe rien et ne modifie pas la base de registre. Pour tester
les changements à chaud avec le pilote v7, il faudra fermer les hôtes ASIO,
enregistrer explicitement cette DLL dans un chemin de test versionné, puis
restaurer l'enregistrement précédent après les essais. Cette opération exige
des droits administrateur et n'a pas encore été faite.

Le manifeste `tests/results/test-build-manifest.json` donne les chemins,
horodatages et SHA-256 des composants compilés. Les contrats OpenAPI et schéma
du moteur de test correspondent aux sources. L'application portable embarque
la même image du moteur de test et le même JavaScript React. Le préflight de la
distribution 1.1.1 ne passe pas avec le moteur canonique ancien ; ces fichiers
ne constituent donc pas une mise à jour publiée ni une validation du mode 6.
