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

## Fichiers Windows

- `Timox VASIO Control 1.0.0.exe` : application portable x64.
- `Timox VASIO Control Setup 1.0.0.exe` : installateur x64.

La publication est destinée au dépôt public [TimoxVasio](https://github.com/timox/TimoxVasio).
