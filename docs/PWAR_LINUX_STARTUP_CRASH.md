# PWAR Linux : SIGSEGV au démarrage signalé le 24 octobre

Symptôme utilisateur : crash au démarrage, SIGSEGV dans une routine de type
`memcpy … unaligned … AVX-512`. Pas encore de backtrace ni de provenance du
binaire utilisateur. Ce nom de routine ne démontre pas un défaut d'alignement.

Inspection de la base locale PWAR `ed5b07c44c7635d006422d646d56368600504b3f` :

- `linux/libpwar.c:on_process` reçoit `in` de `pw_filter_get_dsp_buffer` et
  l'utilise dans les copies sans vérifier s'il est nul, y compris en passthrough.
- `process_ping_pong` et `stream_buffer` copient `n_samples` flottants dans
  `packet.samples[0]`, limité à 128. `n_samples` vient du quantum PipeWire sans
  contrôle de cette limite. Pour un quantum supérieur à la capacité du paquet,
  la copie peut déborder la structure sur la pile.
- Le CLI et la GUI utilisent cette bibliothèque commune. Remplacer la GUI
  par le CLI ne garantit pas d'éviter le crash.

Ces défauts sont constatés dans le source ; leur responsabilité dans le crash
utilisateur n'est pas encore prouvée. Demande envoyée : backtrace et origine du
binaire (paquet installé ou build local). Pas de désactivation AVX proposée.

Prochain travail : reproduire les cas entrée nulle et quantum >128 avec un banc
du callback réel, ajouter les protections et vérifier les chemins normaux.
Un test Linux/PipeWire et la trace utilisateur restent nécessaires pour conclure.
Le paquet Windows PWAR à six patches reste candidat et ne contient pas de
correctif Linux pour ces deux problèmes.
