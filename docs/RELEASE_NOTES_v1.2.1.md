# TimoxVasio 1.2.1

Cette version ajoute des mesures réelles par étape dans l’onglet Performance. La collecte supplémentaire est désactivée au démarrage et s’active dans cet onglet.

## Changements

- Le panneau Performance conserve le budget du callback physique en première position et affiche, auprès du trajet audio, les durées observées pour l’appel de l’application ASIO, la copie dans l’anneau virtuel, l’attente d’un bloc complet, la lecture des clients, le routage et la copie vers le tampon du pilote physique.
- Chaque durée comporte une moyenne, un maximum et le nombre d’échantillons observés dans une fenêtre complète de cinq secondes. La collecte échantillonne au plus un callback sur seize. Une étape sans observation reste vide.
- La commande WebSocket documentée `performance.measurements.set` contrôle la collecte ; `GET /api/v1/state` publie son état et la dernière fenêtre. La commande est idempotente.
- Le transport partagé v9 associe les horodatages aux blocs et à leur époque audio. La DLL et le moteur 1.2.1 doivent être installés ensemble. Les applications audio déjà ouvertes doivent être relancées pour charger la nouvelle DLL.
- Aucun réglage automatique du tampon, de la phase ou du routage n’est ajouté.

## Portée des mesures

Le temps de l’appel ASIO couvre tout le travail de l’application pendant cet appel. L’attente dans l’anneau va de la publication à la lecture du même bloc complet. La copie physique s’arrête à la remise au tampon du pilote : ces chiffres ne mesurent ni la latence du matériel ni le moment où le son est entendu. Les maxima de plusieurs étapes ne sont pas additionnés en une latence totale.

## Téléchargements Windows x64

| Fichier | Contenu |
| --- | --- |
| `TimoxVasio.Driver.1.2.1.zip` | Pilote ASIO, installateur et script d’enregistrement. |
| `Timox.VASIO.Control.Setup.1.2.1.exe` | Installateur de l’interface et du moteur. |
| `Timox.VASIO.Control.1.2.1.exe` | Interface portable et moteur. |
| `MANIFEST.json` | Commit, versions et empreintes SHA-256. |

Installer le pilote depuis le ZIP, puis Control. Le [guide d’installation](INSTALL.md) détaille ces étapes.
