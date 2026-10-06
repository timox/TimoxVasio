# TimoxVasio 1.0.0 — notes de version

**Français** | [English](RELEASE_NOTES_v1.0.0.en.md)

Première version publique du projet, organisée autour de ses trois composants :
le pilote ASIO `TimoxVasio`, le moteur `TimoxVirtualAsioEngine` et l’interface
Electron `Timox VASIO Control`.

## Inclus

- Pilote ASIO virtuel x64 avec 256 entrées et 256 sorties annoncées.
- Moteur de transport audio partagé entre plusieurs applications et routage
  vers un pilote ASIO physique.
- Interface de configuration par l’API documentée, avec profils de canaux par
  application, matrice de routage, Swagger et journaux `info`/`debug`.
- Paquet Electron portable et installateur Windows x64.
- Licence GPL-3.0-only, notice de licence du projet et licence du SDK ASIO
  incluses dans les ressources légales du paquet Electron.
- Revue UX/UI fondée sur cinq captures de la configuration et du routage,
  publiée dans [UX_UI_REVIEW.md](UX_UI_REVIEW.md).
- Guide de prise en main avec schéma d’architecture complet et exemples API :
  [quick start](https://github.com/timox/TimoxVasio/blob/main/docs/API_QUICKSTART.md).
- Soutien au développement via [GitHub Sponsors](https://github.com/sponsors/timox),
  décrit dans [Soutenir le projet](https://github.com/timox/TimoxVasio/blob/main/docs/FUNDING.md).

## Validation effectuée sur cette version

- Le test audio de bout en bout a été confirmé par l’utilisateur après la
  réinstallation de la version 1.0.0 : Renoise était connecté avec 64 entrées
  et 64 sorties; le moteur utilisait SSL ASIO Driver 1 à 48 kHz et 1024 frames,
  avec huit routes actives. L’utilisateur a confirmé le son.
- Le flux `audio.meter` a fourni 60 relevés en 3,5 secondes sur les sorties
  physiques routées et les sorties virtuelles sources. Les crêtes observées
  allaient de `-101,65` à `-26,43 dBFS`.
- Les journaux et l’API de diagnostics ont été vérifiés après réinstallation :
  démarrage du moteur, configuration du pilote physique et application des
  huit routes sont consignés dans
  `%LOCALAPPDATA%/TimoxVasio/logs/engine.log`.
- Swagger a été vérifié par l’utilisateur dans l’interface Electron installée.

Les profils de canaux prennent effet au prochain démarrage de chaque
application audio concernée.

## Fichiers de la release Windows x64

| Asset GitHub | Rôle |
|---|---|
| `Timox.VASIO.Control.Setup.1.0.0.exe` | Installateur par utilisateur de **Timox VASIO Control**. Installe l’interface Electron et son moteur empaqueté. Il n’installe ni n’enregistre le pilote ASIO `TimoxVasio.dll`. |
| `Timox.VASIO.Control.1.0.0.exe` | Application portable : interface Electron et moteur empaqueté, sans installation de l’interface. Le pilote ASIO doit être installé séparément. |
| `TimoxVasio.dll` | Pilote ASIO virtuel chargé par les hôtes audio. Ce fichier seul ne s’installe pas : télécharge aussi le code source de la release et suis la section « Installer la DLL téléchargée » dans [INSTALL.md](https://github.com/timox/TimoxVasio/blob/main/INSTALL.md). |
| `TimoxVirtualAsioEngine.exe` | Exécutable du moteur audio utilisé par l’interface. La version empaquetée est incluse dans les deux applications ci-dessus; cet asset séparé sert au déploiement ou diagnostic manuel. |
| `SHA256SUMS.txt` | Sommes SHA-256 des quatre binaires précédents, pour vérifier leur intégrité après téléchargement. |

Pour une installation normale, installer d’abord **Timox VASIO Control** avec le Setup, puis installer et enregistrer séparément `TimoxVasio.dll` en suivant [INSTALL.md](https://github.com/timox/TimoxVasio/blob/main/INSTALL.md). Les hôtes ASIO chargent la DLL; l’interface configure le moteur, qui est lancé avec elle.

Cette release est publiée dans le dépôt public [timox/TimoxVasio](https://github.com/timox/TimoxVasio).
