# Compatibilité Mixxx avec 256 canaux ASIO

## Diagnostic établi

Mixxx 2.6 beta x64 n’affiche pas `TimoxVasio` parce que sa représentation
interne d’un nombre de canaux ne peut pas représenter 256.

Le journal de la build installée identifie le commit Mixxx
`2.6-beta-402-ge1c1e5b72b`. Le code de ce commit confirme le chemin suivant :

1. PortAudio renseigne `PaDeviceInfo::maxInputChannels` et
   `maxOutputChannels` à partir des nombres renvoyés par le pilote ASIO.
2. `SoundDevicePortAudio` convertit chacun de ces `int` en
   `mixxx::audio::ChannelCount`.
3. Dans `src/audio/types.h`, `ChannelCount::value_t` est `uint8_t` et
   `valueFromInt()` accepte au plus `std::numeric_limits<uint8_t>::max()`, soit
   255. La valeur 256 devient donc la valeur sentinelle invalide 0.
4. `SoundManager::getDeviceList()` écarte un périphérique dont les nombres de
   canaux d’entrée et de sortie sont tous deux invalides.

TimoxVasio annonce volontairement 256 entrées et 256 sorties conformément au
contrat ASIO du projet. Mixxx reçoit donc 256/256 de PortAudio, transforme ces
deux valeurs en compteurs invalides, puis retire le périphérique de la liste.
Cela correspond à l’absence constatée dans la liste de l’interface. Le
problème se situe après l’énumération ASIO, dans la limite de représentation de
Mixxx.

## Éléments comparés

- Le probe PortAudio compilé dans ce dépôt énumère `TimoxVasio` avec
  256 entrées, 256 sorties et un format accepté à 48 kHz quand le moteur est
  verrouillé à 48 kHz.
- Le pilote renvoie 256/256 via `getChannels()` et fournit les informations de
  canaux demandées par PortAudio.
- Le source Mixxx correspondant au commit indiqué dans son journal contient
  la conversion `ChannelCount(int)` et le filtrage décrit ci-dessus.
- Le journal Mixxx montre qu’il démarre sur VB-Matrix. Le PID Mixxx recensé
  auparavant par le moteur TimoxVasio ne prouvait pas que Mixxx avait conservé
  le périphérique dans sa liste.

## Correction requise côté hôte

Pour que cette build de Mixxx affiche et utilise les 256 canaux, Mixxx doit
représenter au moins 256 dans `ChannelCount` et préserver cette valeur dans les
chemins de sélection et d’allocation de canaux. Une modification du pilote
TimoxVasio qui annonce 255 masquerait le symptôme en réduisant la capacité
annoncée et ne satisferait pas le contrat 256/256.

Un patch minimal qui élargit `ChannelCount::value_t` à `uint16_t` est fourni
dans [`patches/mixxx/0001-audio-channel-count-support-256.patch`](../patches/mixxx/0001-audio-channel-count-support-256.patch).
Il a été appliqué uniquement à une copie locale du source Mixxx 2.7 sous
`vendor/mixxx-2.7-256`; la compilation x64 a produit
`build_mixxx_2.7_256/mixxx.exe`. Le 4 octobre, l’utilisateur a lancé cette
copie et confirmé qu’elle découvre TimoxVasio. Cela vérifie la découverte,
mais pas encore l’ouverture du flux ni la capacité effectivement utilisable
dans Mixxx. Cette copie corrigée est distincte de la version installée :
Mixxx 2.6 beta, commit `2.6-beta-402-ge1c1e5b72b`, reste sans le patch.

Lors du premier essai, le moteur Timox était arrêté et n’avait pas d’horloge
physique configurée; TimoxVasio annonçait alors sa fréquence de repli de
44,1 kHz. Le moteur a ensuite été configuré par `configuration.apply` avec
`SSL ASIO Driver 1`, à la fréquence courante confirmée de 48 kHz et à sa taille
préférée de 1024 frames. L’état API est `stopped` sans route, et aucun client
Mixxx n’est encore attaché. Il faut réinitialiser le périphérique ASIO dans
Mixxx pour vérifier qu’il reprend 48 kHz; l’ouverture et le transfert audio
restent à valider.

Le dépôt TimoxVasio ne modifie pas l’installation de Mixxx. La validation avec
Mixxx à pleine capacité reste conditionnée à l’installation explicite d’une
version corrigée et reconstruite, puis à la vérification de son énumération et
de l’ouverture du flux. L’installation actuelle de Mixxx reste incompatible
avec les pilotes ASIO qui annoncent exactement 256 canaux dans chaque
direction.
