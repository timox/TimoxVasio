# TimoxVasio 1.0.0 — notes de version

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

## Limites connues

- Le parcours audio de bout en bout a été testé et confirmé par l’utilisateur
  le 5 octobre 2026. Les détails instrumentés et les niveaux mesurés ne sont
  pas consignés dans ces notes; l’ancienne lecture à `-120 dBFS` était antérieure
  à cet essai et ne permettait pas, à elle seule, de conclure.
- Les captures UX/UI fournies précèdent la build renommée et les corrections
  finales. Le clavier, le lecteur d’écran et le contraste calculé ne sont pas
  couverts par cette revue.
- Les profils de canaux ne prennent effet qu’au prochain démarrage de chaque
  application audio concernée.

## Fichiers de la release Windows x64

| Asset GitHub | Rôle |
|---|---|
| `Timox.VASIO.Control.Setup.1.0.0.exe` | Installateur par utilisateur de **Timox VASIO Control**. Installe l’interface Electron et son moteur empaqueté. Il n’installe ni n’enregistre le pilote ASIO `TimoxVasio.dll`. |
| `Timox.VASIO.Control.1.0.0.exe` | Application portable : interface Electron et moteur empaqueté, sans installation de l’interface. Le pilote ASIO doit être installé séparément. |
| `TimoxVasio.dll` | Pilote ASIO virtuel chargé par les hôtes audio. Ce fichier seul ne s’installe pas : télécharge aussi le code source de la release et suis la section « Installer la DLL téléchargée » dans [INSTALL.md](https://github.com/timox/TimoxVasio/blob/main/INSTALL.md). |
| `TimoxVirtualAsioEngine.exe` | Exécutable du moteur audio utilisé par l’interface. La version empaquetée est incluse dans les deux applications ci-dessus; cet asset séparé sert au déploiement ou diagnostic manuel. |
| `SHA256SUMS.txt` | Sommes SHA-256 des quatre binaires précédents, pour vérifier leur intégrité après téléchargement. |

Pour une installation normale, installer d’abord **Timox VASIO Control** avec le Setup, puis installer et enregistrer séparément `TimoxVasio.dll` en suivant [INSTALL.md](https://github.com/timox/TimoxVasio/blob/v1.0.0/INSTALL.md). Les hôtes ASIO chargent la DLL; l’interface configure le moteur, qui est lancé avec elle.

La publication est destinée au dépôt public [TimoxVasio](https://github.com/timox/TimoxVasio).
