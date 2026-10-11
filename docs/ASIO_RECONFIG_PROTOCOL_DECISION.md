# Changement de fréquence et de buffer pendant une session ASIO

## Résultat observé

Le mode 6 avec deux clients a changé le moteur de 48 kHz / 1 024 trames à
44,1 kHz / 1 024 trames, puis 44,1 kHz / 512 trames. Les clients ont continué
à recevoir des callbacks, mais aucun n'a reçu `kAsioResetRequest` ni
`sampleRateDidChange`. Leurs buffers étaient donc restés dimensionnés pour
l'ancien format. La preuve est dans
`tests/results/multiclient-20261007-215857-234-6b8ab83c/summary.json`.

## Contrat ASIO

`asiosdk/common/asio.h` définit `kAsioResetRequest` pour demander à l'hôte de
fermer puis rouvrir le pilote après un changement de fréquence ou d'un autre
paramètre fondamental. Le SDK indique d'utiliser également cette demande
pour le changement de taille de buffer, car `kAsioBufferSizeChange` n'est pas
pris en charge. Le SDK précise que la demande est transmise à l'application :
le pilote ne peut pas garantir qu'elle sera acceptée. Le banc de test peut
vérifier la notification et le redémarrage de ses propres hôtes, mais le
comportement de Mixxx, Renoise et SuperCollider doit être testé séparément.

## Deux choix pour le produit

1. **Autoriser une exception minimale dans la DLL** : lorsqu'elle constate
   que le format confirmé par le moteur diffère du format de ses buffers ASIO,
   elle cesse de traiter les anciens buffers et demande à l'hôte un
   `kAsioResetRequest`. L'émission doit se faire hors du callback audio et
   gérer la possibilité que l'hôte ferme immédiatement le pilote. La DLL ne
   ferait toujours aucun inventaire, n'ouvrirait aucun autre pilote et
   n'écrirait aucun fichier. Elle publierait aussi le compteur de demandes
   dans son bloc d'état. Une conception de durée de vie COM et de thread doit
   être validée avant cette modification.
2. **Conserver la limite stricte « publication d'état seulement »** : le
   moteur refuserait un changement de fréquence ou de buffer tant qu'un
   client virtuel est actif, avec un message invitant à fermer les clients
   puis à appliquer le nouveau format. Le mode 6 serait déclaré non pris en
   charge plutôt que réussi ; cela éviterait le fonctionnement silencieux
   avec un ancien format.

Le 9 octobre 2026, l'utilisateur a autorisé la première option. Une DLL de
test a été compilée : son thread de rappel cesse d'utiliser les anciens
buffers dès qu'il constate un changement de fréquence ou de taille, puis
demande `kAsioResetRequest` depuis un travailleur Windows distinct. La demande
incrémente le compteur du bloc d'état v7. Le test local
`DriverAudioProbe --expect-reset` a reçu une seule demande et rouvert ses
buffers à 44,1 kHz / 512 trames. Le test ordinaire de transport passe aussi.
Cette DLL n'est ni installée ni enregistrée. Le mode 6 avec plusieurs clients
et les hôtes réels reste à rejouer avant toute conclusion de conformité.

Le 9 octobre, `vasio-probe loopback` a aussi été préparé pour répondre à
`kAsioResetRequest` par arrêt, destruction des buffers, fermeture et réouverture
du pilote, puis création des buffers au format courant et redémarrage. Le
rapport distingue la notification du redémarrage achevé ; le script du mode 6
exige un redémarrage après chacun des deux changements. Le probe compile, mais
ce parcours complet n'a pas encore été exécuté avec plusieurs clients. La
DLL de test a été recompilée après correction de la récupération de son thread
de callback arrêté. L'inventaire `vasio-doctor` du 9 octobre voit encore trois
clients utilisant la DLL v6 installée ; aucun test live ni remplacement de DLL
n'a été entrepris pendant leur session.
