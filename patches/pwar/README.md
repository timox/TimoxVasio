# PWAR : correctifs séparés et qualification

Base amont : `ripxorip/PWAR`, commit
`ed5b07c44c7635d006422d646d56368600504b3f`.
Le patch modifie le routeur PWAR, pas TimoxVasio.dll. Il conserve les structures
de paquets et les échantillons des échanges valides testés. Il n'est pas encore
intégré à une DLL PWAR ni qualifié avec PipeWire sur le réseau.

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
