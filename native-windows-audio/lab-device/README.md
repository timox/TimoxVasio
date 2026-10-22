# Transaction du périphérique de laboratoire

Ce sous-projet contient pour l'instant la transaction pure et ses tests avec un
faux Windows. Il ne fournit pas encore d'exécutable installant un périphérique.

```powershell
cmake -S native-windows-audio/lab-device -B build_native_lab_device -A x64
cmake --build build_native_lab_device --config Release
ctest --test-dir build_native_lab_device -C Release --output-on-failure
```

Le reçu distingue préparation, enregistrement, retrait en attente et retrait
terminé. L'intention est persistée avant l'enregistrement ; toute récupération
vise uniquement l'instance exacte enregistrée dans le reçu de cette machine.
Un reçu de retrait terminé n'autorise pas la suppression d'une instance qui
réapparaît. Les doublons, y compris non présents, empêchent la création.

Validation du 22 octobre 2026 : RED initial observé, puis deux défauts de reprise
identifiés en revue, reproduits et corrigés. Compilation parent MSVC `/W4 /WX`
et CTest 1/1 réussis ; revue finale indépendante favorable. Le test de concurrence
vérifie qu'un périphérique étranger reste présent après retrait de notre instance.

Ces tests ne prouvent pas la durabilité sur disque, les ACL, le refus des points
de réanalyse ou les opérations SetupAPI réelles : le wrapper Windows reste à
implémenter et l'installation devra être qualifiée dans la VM dédiée.

Plan : [paquet laboratoire VM](../../docs/superpowers/plans/2026-10-22-native-audio-vm-lab-package.md).
