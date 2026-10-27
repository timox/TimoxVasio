# PWAR — candidat de qualification Windows x64

Ce paquet contient la DLL PWAR corrigée et testée localement avec l'hôte ASIO
Timox. Il ne contient pas TimoxVasio.dll et ne l'installe pas. Les essais avec
un véritable pair Linux, la stabilité sur durée et les performances restent
à qualifier. Le protocole PWAR existant est conservé.

## Préparation Windows

1. Extraire tout le paquet dans un dossier durable. Ne pas déplacer sa DLL après
   enregistrement : Windows conserve son chemin.
2. Pour installer PWAR, ouvrir PowerShell **en administrateur**, se placer dans
   ce dossier puis exécuter explicitement :

   ```powershell
   & "$env:windir\System32\regsvr32.exe" (Join-Path $PWD 'PWARASIO.dll')
   ```

   Cette commande enregistre le pilote ASIO et sa classe COM pour la machine.
   L'extraction du ZIP, à elle seule, ne modifie pas Windows.
3. Relancer le moteur et la GUI Timox afin de rafraîchir l'inventaire ASIO.
4. Dans Routing, clic droit sur le fond → **Configure PWAR / PipeWire…**.
   Saisir l'IPv4 Linux et le port 8321, puis enregistrer. Sélectionner PWAR dans
   le brouillon et appliquer la configuration. Si PWAR était déjà ouvert,
   utiliser **Reopen PWAR driver** après enregistrement des réglages.

PWAR transporte les canaux de cet endpoint ASIO quand le moteur l'ouvre et les
route. Il ne recherche ni ne redirige l'audio des autres applications Windows.

Pour désenregistrer ce même pilote, après fermeture des applications qui
l'utilisent, exécuter dans le même dossier et avec les mêmes droits :

```powershell
& "$env:windir\System32\regsvr32.exe" /u (Join-Path $PWD 'PWARASIO.dll')
```

## Essai Linux

Un crash Linux au démarrage dans une routine `memcpy` a été signalé pendant
la préparation de ce candidat. Le source Linux fourni ne corrige pas encore
le crash utilisateur de façon confirmée. Le correctif 0007 protège désormais
le callback contre une entrée nulle et un quantum réseau supérieur à 128 : il
produit du silence et compte un xrun dans ces cas. Les blocs plus grands restent
à prendre en charge par fragmentation. La trace du crash et un essai PipeWire
réel restent nécessaires. GUI et CLI partagent cette bibliothèque ; le CLI
n'est donc pas un contournement garanti. Aucun binaire Linux corrigé n'est inclus.

Le source amont épinglé est fourni sous `sources/PWAR`, avec ses instructions
Linux dans son README. L'interface ou le CLI PWAR doit envoyer vers l'IPv4
Windows, port UDP 8321. Exemple avec un CLI déjà construit :

```sh
pwar_cli --ip ADRESSE_IPV4_WINDOWS
```

Commencer avec 48000 Hz et des blocs ASIO de 128 frames. Relier une source audio
dans PipeWire, puis contrôler les vumètres et le retour audio dans Timox.
Un pilote « ouvert » ne confirme pas la réception réseau. Le réseau et les
pare-feux doivent permettre l'échange UDP ; le paquet ne les modifie pas.

## Sources et provenance

- Base PWAR : `ripxorip/PWAR`, commit `ed5b07c44c7635d006422d646d56368600504b3f`.
- Sept correctifs fournis sous `patches`, déjà appliqués dans `sources/PWAR`.
- Journalisation UDP de diagnostic désactivée par défaut.
- Sources SDK ASIO nécessaires fournies sous `sources/asiosdk/common`, avec leurs
  notices. Licence PWAR dans `sources/PWAR/LICENSE` et `GPL-3.0.txt`.
- `MANIFEST.json` contient les empreintes de la DLL et des fichiers du paquet.

Reconstruction avec CMake et Visual Studio C++ x64, sans télécharger de source :

```powershell
cmake -S sources -B build -A x64
cmake --build build --config Release
```

Résultat : `build/Release/PWARASIO.dll`. Les chemins de build et les informations
de liaison peuvent changer son empreinte ; le manifeste identifie la DLL livrée.
Les instructions de compilation Linux amont sont conservées dans le source ;
aucun exécutable Linux n'est inclus dans ce paquet.
