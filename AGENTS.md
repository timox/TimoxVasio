# Repères du dépôt TimoxVasio

## Dépôt actif
- Racine : dossier retourné par `git rev-parse --show-toplevel` dans ce clone.
- Limiter les modifications à ce dépôt public.
- Avant toute compilation ou installation, confirmer la racine avec `git rev-parse --show-toplevel` et lire `git status`.

## Binaires : fonctions et chemins canoniques
| Composant | Fonction | Build | Distribution |
|---|---|---|---|
| `TimoxVasio.dll` | Pilote ASIO chargé par les applications | `build_driver_110\Release\TimoxVasio.dll` | `gui\dist\TimoxVasio Driver 1.1.0.zip` |
| `TimoxVirtualAsioEngine.exe` | Moteur audio et serveur API | `build_codex_110\Release\TimoxVirtualAsioEngine.exe` | Embarqué sous `resources\backend\` dans les deux exécutables Electron |
| `Timox VASIO Control` | Interface Electron | Sources `gui\` | `gui\dist\Timox VASIO Control Setup 1.1.0.exe` et `gui\dist\Timox VASIO Control 1.1.0.exe` |

Electron utilise le moteur de `build_codex_110\Release` en développement et pendant l’empaquetage; empaqueté, il le lance sous `process.resourcesPath\backend`. Le Setup Electron installe l’interface et le moteur, mais n’installe ni n’enregistre la DLL ASIO, distribuée séparément dans le ZIP.

Le pilote 1.1.0 s’installe sous `C:\Program Files\Steinberg\VirtualASIO\1.1.0\TimoxVasio.dll`. Ce chemin versionné permet de l’enregistrer même si une application conserve une ancienne DLL ouverte. Cette application doit être relancée pour charger la nouvelle version.

## Provenance
- Ne jamais choisir un binaire d’après son nom seul. Vérifier son chemin, sa date et son SHA-256.
- Comparer le moteur empaqueté au moteur compilé et le pilote du ZIP au pilote compilé.
- En cas de fichier absent ou d’empreinte différente, arrêter le packaging et reconstruire le composant concerné.
- `gui\dist\win-unpacked\resources\backend\TimoxVirtualAsioEngine.exe` est une copie de contrôle, pas un livrable séparé.
