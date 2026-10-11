# Inventaire en direct du 9 octobre 2026

Rapport : `tests/results/doctor-live-20261009.json`, produit par
`vasio-doctor.ps1 --output` pendant que Mixxx, Renoise et scsynth utilisaient
le moteur à 48 kHz / 1 024 échantillons. Le doctor n'a changé aucun réglage.

- Les trois clients utilisent encore la zone partagée v6. Leurs compteurs de
  rappel, horodatages QPC, identité de DLL et demandes de réinitialisation ne
  sont donc pas vérifiables par `vasio-status` v7. Le fichier installé au
  chemin `C:\Program Files\Steinberg\VirtualASIO\1.1.1\TimoxVasio.dll` a le
  SHA-256 `2AFF5C350D8699B0AC534F21806A0B4B7A5F722A56BF080A9AAF944EEAF3E8D9`.
- Les 18 routes configurées alimentent les entrées 1–10 de scsynth. Ses
  entrées 11–12 sont ouvertes mais sans écrivain routé. Renoise ouvre 64
  entrées sans écrivain routé ; ses sorties 1–2 alimentent scsynth. Le
  diagnostic `UNWRITTEN_INPUT` décrit donc des voies ouvertes sans route, pas
  la preuve d'un défaut audible sur les voies utilisées.
- Sur deux lectures espacées de trois secondes, le compteur cumulé des
  opérations de buffer manquées n'a pas augmenté : Mixxx 1 → 1, Renoise
  1 030 → 1 030, scsynth 80 → 80. Cette courte fenêtre ne remplace pas un
  test de cadence de 60 minutes.
- L'inventaire Windows et USB a réussi lors de l'exécution avec accès aux
  informations système. Quatorze processus restent ininspectables ; la
  recherche de DLL chargées n'est donc pas exhaustive. Windows MIDI Services
  renvoie `REGDB_E_CLASSNOTREG` ; 43 ports WinMM sont visibles.

Ce rapport concerne l'installation 1.1.1 actuellement chargée, et non les
binaires v7 de test. Les essais de la nouvelle logique de changement de
routes sur la SSL restent à faire lorsque ces clients auront quitté la
session audio.
