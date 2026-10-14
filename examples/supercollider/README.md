# Exemple : compresseur multibande SuperCollider sur TimoxVasio

Démonstration simple : un DAW (Renoise, Mixxx, etc.) envoie sa sortie sur TimoxVasio,
SuperCollider reçoit les deux canaux ASIO, les traite, et renvoie le son.

```
DAW ──► TimoxVasio (sortie 1-2) ──► SuperCollider (entrées 1-2)
                                        │  séparation 3 bandes (Linkwitz-Riley 4)
                                        │  compresseur par bande + volumes + limiteur
                                        ▼
                         SuperCollider (sortie 1-2) ──► sortie choisie
```

## Fichiers

| Fichier | Rôle |
|---|---|
| `timox_compresseur.scd` | Serveur SuperCollider, SynthDef `\timoxCompresseur`, interface graphique |
| `pilotage/pilote.mjs` | Pilotage en Node.js de l'API TimoxVasio et du compresseur |

## Prérequis

- SuperCollider 3.13 pour Windows, avec le support ASIO.
- TimoxVasio installé et enregistré (voir [INSTALL.md](../../INSTALL.md)).
- Node.js 18 ou plus pour le pilotage (22 ou plus pour le WebSocket natif).

## Lancer le compresseur

1. Ouvrir `timox_compresseur.scd` dans SuperCollider.
2. Placer le curseur dans le bloc entre parenthèses et lancer avec Ctrl+Entrée.
3. Le serveur démarre sur le périphérique `TimoxVasio`, deux entrées et deux sorties,
   puis la fenêtre de réglages s'ouvre.

Le nom du périphérique se règle avec la variable d'environnement `TIMOX_DEVICE` si la
carte n'est pas nommée `TimoxVasio`. Les noms disponibles sont listés par
`ServerOptions.devices` dans sclang.

Côté DAW, la sortie stéréo doit être envoyée sur TimoxVasio. Côté routage, Timox VASIO
Control relie la sortie de l'application aux entrées de `scsynth.exe`, puis la sortie de
`scsynth.exe` vers la sortie physique voulue.

## Réglages

- **Entrée / Sortie** : gains en dB, lissés pour éviter les clics.
- **Coupures** : 200 Hz (bas/médium) et 2 kHz (médium/aigu).
- **Seuil et ratio** de chaque bande. Au-dessus du seuil, le gain baisse selon le ratio.
- **Bypass** : envoie l'entrée sans traitement.

## Piloter depuis Node.js

```sh
# API TimoxVasio : port annoncé par le moteur
export TIMOX_API=http://127.0.0.1:52341
node pilotage/pilote.mjs etat        # état du moteur, clients, routes
node pilotage/pilote.mjs demarrer    # engine.start
node pilotage/pilote.mjs arreter     # engine.stop

# SuperCollider : scsynth sur 127.0.0.1:57110 par défaut (SC_HOTE, SC_PORT)
node pilotage/pilote.mjs sc-demarrer          # crée le compresseur (nœud 1000)
node pilotage/pilote.mjs sc-regler loThr -30  # règle un paramètre
node pilotage/pilote.mjs sc-bypass 1          # bypass activé
node pilotage/pilote.mjs sc-arreter           # libère le nœud
```

Le nœud 1000 est partagé avec la fenêtre : ce qui est réglé depuis Node est visible
dans les curseurs au prochain mouvement, et inversement.

## Limites

- Démonstration, pas un outil de mastering : un seul compresseur par bande, sans
  réglage d'attaque ni de relâchement.
- Le traitement ajoute un bloc de latence, comme tout passage par un pilote ASIO.
- Testé hors ligne : le traitement a été vérifié par rendu NRT de scsynth sur un signal
  de test (la partie forte est réduite de 6,6 dB, la partie faible est inchangée). Le
  test sur la carte TimoxVasio réelle et le fonctionnement de l'interface graphique ne
  sont pas vérifiés dans cet environnement.
- Le pilotage WebSocket de l'API n'a pas été testé contre un moteur réel.
