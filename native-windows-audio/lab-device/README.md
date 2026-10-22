# Outil de périphérique laboratoire

Ce projet autonome prépare un utilitaire x64 SetupAPI pour la VM Windows de test.
Le wrapper natif est du code à qualifier : aucun create/remove réel n'a été
exécuté sur l'hôte. Les tests automatiques couvrent la transaction simulée,
le parsing, le codec et la politique de métadonnées ; ils ne prouvent pas le
comportement de SetupAPI, du système de fichiers ou des ACL réelles.

```powershell
cmake -S native-windows-audio/lab-device -B build_native_lab_device -A x64
cmake --build build_native_lab_device --config Release
ctest --test-dir build_native_lab_device -C Release --output-on-failure
```

L'outil est lié au runtime MSVC statique. Son aide et les tests purs sont les
seules commandes CTest ; aucune installation ni élévation automatique.

```text
TimoxNativeLabDevice --help
TimoxNativeLabDevice inspect
TimoxNativeLabDevice create
TimoxNativeLabDevice remove
```

Sans argument : aide. `create` et `remove` sans `--apply` ne créent aucun dossier,
fichier ou device. Ils lisent l'inventaire et le reçu existant, avec refus si
inaccessible. Le résultat dry-run est un constat instantané ; l'application
refait les contrôles sous verrou. `create --apply` et `remove --apply` sont
réservés à la VM explicitement autorisée et exigent un token Administrateurs
effectif. L'outil ne prétend pas détecter de manière fiable une VM ; la cible
reste une responsabilité de l'opérateur. Aucune commande ne signe ou installe
un INF, ne modifie TESTSIGNING/Secure Boot, ni ne touche au pilote ASIO.

Le reçu fixe `%ProgramData%\TimoxVasio\NativeAudioLab\receipt.bin` contient
84 octets versionnés : identifiants GUID d'opération et de machine, étape.
L'instance est dérivée du préfixe fixe `ROOT\TimoxWindowsAudioLab\` ; le hardware
ID reste `Root\TimoxWindowsAudio`, classe MEDIA. L'identité MachineGuid est
une corrélation contre les erreurs de cible : une VM clonée ou un administrateur
peut la reproduire. Ce n'est pas une preuve cryptographique de machine.

Les dossiers privés et fichiers doivent appartenir à SYSTEM/Administrateurs,
avec DACL protégée limitée à ces deux trustees. Les reparse points et fichiers
multi-liens sont refusés. Les handles de dossiers restent ouverts sans partage
DELETE ; un fichier de verrou exclusif sérialise les instances de cet outil.
Les autres installateurs Windows ne participent pas à ce verrou : un inventaire
après enregistrement détecte un doublon et retire uniquement notre instance.
Un ancien reçu terminal ne permet pas de supprimer une instance réapparue.

La publication du reçu écrit un fichier temporaire propre, appelle
FlushFileBuffers puis MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH). Elle ne
constitue pas une transaction SetupAPI/filesystem et ne promet pas la survie
à toute panne disque ou coupure électrique. Un reçu absent ou invalide entraîne
un refus de récupération automatique ; l'opérateur conserve le snapshot VM.
Un fichier temporaire orphelin n'est jamais adopté ni supprimé par joker.
Après suppression, une erreur d'écriture ou un redémarrage nécessaire laisse
une intention de récupération. Les droits ne visent pas à résister à un
administrateur hostile qui peut de toute façon modifier les devices.

Code 0 : succès/dry-run ; 2 : refus/arguments ; 3 : erreur Windows/récupération ;
3010 : redémarrage requis, jamais déclenché automatiquement. Conserver JSON et
instance affichée. La commande de retrait ne supprime pas le paquet DriverStore :
après retrait confirmé, PnPUtil utilise exclusivement le nom `oemNN.inf` enregistré
lors de l'installation. Voir le plan
`docs/superpowers/plans/2026-10-22-native-audio-vm-lab-package.md`.
