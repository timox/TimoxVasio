# PWAR : correctifs séparés et qualification

Base amont : `ripxorip/PWAR`, commit
`ed5b07c44c7635d006422d646d56368600504b3f`.
Les patches modifient PWAR, pas TimoxVasio.dll. Ils conservent les structures
de paquets et les échantillons des échanges valides testés. Une DLL séparée est
compilée pour qualification locale ; elle n'est ni installée ni intégrée dans
Timox, et la liaison avec PipeWire sur le réseau n'est pas encore qualifiée.

Le code amont porte le copyright 2025 Philip K. Gisslow et sa licence est fournie
dans `GPL-3.0.txt`. Les modifications du routeur sont proposées sous cette même
licence. Aucun code PWAR n'est lié au moteur Timox par ce banc de tests.

## 0001 : contrôles de bornes du routeur

Avant indexation/copie, le receveur vérifie pointeurs, canaux, tailles et nombre
de paquets contre les capacités réelles des tableaux. Le wrapper streaming
vérifie ses pointeurs avant de les lire et utilise une différence de séquences
64 bits ; un paquet refusé reste inchangé. L'émetteur refuse notamment un chunk
nul, un nombre de canaux supérieur à deux et un bloc supérieur à 4096 frames.
Les sorties en erreur gardent un nombre de paquets à envoyer égal à zéro.

Ce patch ne résout pas la division par zéro dans le listener ASIO, qui précède
l'appel au routeur. Il ne résout pas non plus son arrêt concurrent, ses sorties
partielles ni son unique entrée ASIO. Le contrôle de cohérence de tous les
fragments d'une même séquence et les données inutilisées des paquets restent à
examiner. Les pointeurs et capacités de buffers fournis par l'appelant doivent
rester valides pendant tout l'appel.

## Reproduire

Télécharger les sources au commit indiqué dans un dossier de travail distinct.
Conserver leur licence. Avant modification, SHA-256 de `protocol/pwar_router.c`
(octets de l'archive amont, fins de lignes LF) :
`88DA060DE19FF6926A19D7A0545EAD271C8FCAB67FE5FE7DE64F8975DFD376C1`.

Depuis la racine PWAR, appliquer le patch avec son chemin absolu :

```powershell
git apply --check C:/chemin/TimoxVasio/patches/pwar/0001-router-validate-buffer-bounds.patch
git apply C:/chemin/TimoxVasio/patches/pwar/0001-router-validate-buffer-bounds.patch
```

Depuis la racine TimoxVasio, après vérification de la racine et de git status :

```powershell
cmake -S patches/pwar -B build_pwar_qualification -DPWAR_SOURCE_DIR=C:/chemin/PWAR
cmake --build build_pwar_qualification --config Release
ctest --test-dir build_pwar_qualification -C Release --output-on-failure
```

Ce projet CMake séparé compile le véritable `pwar_router.c` de la copie fournie.
Il ne télécharge, n'installe et ne charge aucun pilote. Il évite les cibles
Windows amont, dont les chemins relatifs et la copie post-build vers `Z:` ne
conviennent pas à ce dépôt. Ce n'est pas une correction du build de la DLL.

## Résultats du 24 octobre 2026

- Avant patch : compilation réussie ; test rouge sur l'acceptation d'un paquet
  de zéro frame. Aucun crash provoqué pour cette reproduction.
- Après patch, appliqué avec `git apply --check` puis `git apply` sur une nouvelle
  extraction : compilation MSVC x64 réussie sans avertissement ; CTest 1/1 passe.
- Cas vérifiés : tailles nulles/excessives, indices hors bornes, grandes valeurs
  32 bits, capacité d'émission insuffisante, refus sans modification du routeur
  et du buffer de réception, aller-retour stéréo avec ordre de réception inversé,
  wrapper streaming et blocs maximaux 4096 frames à chunks 64 et 128.

Cette preuve porte sur les fonctions du routeur dans le banc local. Elle ne
qualifie ni l'ensemble des tests amont, ni le transport UDP, le cycle de vie ASIO,
le fonctionnement Linux/Windows ou la latence. L'objectif d'intégration complète
reste décrit dans [l'étude PWAR](../../docs/pwar-integration-assessment.md).

## 0002 : arrêt du listener avant libération

Le constructeur ne lance plus la réception. `start` la lance après préparation
des buffers ; `stop` demande l'annulation puis joint le worker avant de modifier
l'état partagé. `disposeBuffers` attend ce drainage, et le destructeur ferme
l'émetteur après celui-ci. Le drapeau de réception est atomique ; la réception
vérifie l'annulation après un délai socket de 20 ms, y compris sans trafic.
Les datagrammes trop grands et les erreurs UDP récupérables ne ferment pas le
listener. Les mutations de buffers/fréquence pendant la lecture sont refusées.

Le contrat conserve des appels de contrôle sérialisés par l'hôte. `stop` et
`disposeBuffers` refusent l'appel depuis le callback pour éviter un self-join.
Un callback hôte qui ne termine jamais ou un envoi réseau bloqué peut toujours
retenir le drainage : le délai de réception n'est pas une borne universelle
d'arrêt. Le retour d'erreur de bind/startup depuis le worker reste à ajouter.

Appliquer `0002-asio-drain-listener-before-disposal.patch` après 0001 sur la
révision amont, avec les mêmes commandes `git apply --check` puis `git apply`.
Le banc peut construire la DLL et tester directement ses sources Windows :

```powershell
cmake -S patches/pwar -B build_pwar_qualification -DPWAR_SOURCE_DIR=C:/chemin/PWAR -DPWAR_BUILD_WINDOWS_DRIVER=ON -DPWAR_ASIO_SDK_DIR=C:/chemin/asiosdk
cmake --build build_pwar_qualification --config Release
ctest --test-dir build_pwar_qualification -C Release --output-on-failure
```

Le SDK ASIO existant est utilisé par chemin, sans copie ni téléchargement.
La cible ne lance pas les commandes d'installation ou d'enregistrement du pilote.
Le test Windows lie les vraies sources du pilote dans un exécutable : il ne
prouve pas l'activation COM de la DLL construite. Exécuter via CTest, qui configure
un profil de test local avec destination audio 127.0.0.1 ; les logs sont aussi
dirigés vers loopback. Il vérifie que le port UDP 8321 est libre avant de démarrer,
puis l'ouvre temporairement. Aucun flux matériel n'est activé.

Validation observée le 24 octobre :

- Amont : test bloqué à la destruction, timeout CTest à 5 secondes.
- Première correction : la revue a détecté un arrêt sur WSAEMSGSIZE ; test rouge
  reproduit, puis correction et test vert.
- Deux patches appliqués sur une nouvelle extraction : build x64 réussi, puis
  CTest **2/2**, 0,26 seconde. Avertissements amont/SDK toujours présents
  (conversions, noms masqués et anciens exports du fichier .def).
- Contrôles : arrêt sans trafic, port ouvert/libéré, redémarrage, datagramme
  surdimensionné ignoré, libération retenue pendant un callback actif, destruction
  directe d'un listener actif. Revue indépendante sans autre défaut important
  dans ce périmètre.
- `build_pwar_qualification/Release/PWARASIO.dll`, 148992 octets, timestamp UTC
  `2026-10-24T09:36:11`, SHA-256
  `6F792BAF5B3380895CB8F3E65C8CD97878AA1AF3F2BA19046266E68C13ECF443`.
  Exports COM examinés avec dumpbin, sans chargement ni enregistrement de la DLL.

Restent notamment le listener ASIO qui divise avant validation, les sorties
partielles, la capture stéréo, le résultat de démarrage, la cohérence des fragments,
les horodatages partagés et les essais avec Linux. Ce binaire de qualification
ne constitue pas une livraison utilisable du pont.
