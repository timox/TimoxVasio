# Validation des dépendances et du moteur réellement actif

## Défaut observé

Le portable ProcessCopy récent se raccordait au moteur PID 20088 lancé le
26 octobre depuis `build_gui_api_20261026`. La version API seule ne suffisait
pas à prouver que ses capacités correspondaient à l'interface. Au diagnostic
du 28 octobre, ses requêtes HTTP expiraient. La GUI conservait alors un ancien
inventaire de processus. Le PID 21060 désignait réellement Firefox, mais la
liste affichait `nseA579.tmp`. La réutilisation du PID est une hypothèse, pas
une cause démontrée par une réponse API brute.

## Correction dans TimoxVSSC

- `artifacts.lock.json` est inclus dans le paquet. Chaque démarrage vérifie
  les SHA-256 du moteur, du client PWAR et des deux schémas API, sans repli
  vers un checkout de développement.
- Le processus écoutant sur le port 52525 est identifié par PID, date de
  création, chemin absolu et SHA-256. Le mutex moteur est également contrôlé.
- Un moteur conforme et réactif est réutilisé. Un moteur ancien attesté par
  le reçu `engine-ownership.json` est arrêté puis remplacé automatiquement.
  Un processus inconnu est refusé ; aucun second moteur concurrent n'est lancé.
- L'API 1.1, la capacité `processCopyTemplates` et la version 1 du schéma de
  supervision sont exigées avant de rendre la connexion disponible.
- Les attentes HTTP, WebSocket et de démarrage sont bornées. Un rafraîchissement
  des processus invalide l'ancienne liste ; échec, déconnexion et réponse tardive
  ne peuvent pas réintroduire une identité sélectionnable périmée.
- Le profil utilisateur reste conservé et le moteur reprend sa configuration
  validée avant remplacement. Le test réel a toutefois observé une réécriture
  de normalisation : 46 routes sont devenues 48 et les deux ponts WASAPI
  persistés ont disparu du fichier écrit. Le fichier pré-migration est conservé
  dans `build_processcopy_20261027/startup-repair/configuration.at-launch.json`;
  ce point doit être corrigé séparément avant de promettre une conservation
  octet pour octet. Aucun changement de pilote ASIO, de DSP ou de réglage SSL
  n'est inclus.

## Vérification

100 tests Node/Electron/scripts/contrat et 53 tests React passés. La revue a
détecté une course entre confirmation API et inspection du processus : corrigée
avec régression reproduite avant correction. Les tests couvrent aussi l'échec
d'écriture du reçu et une identité obtenue après expiration du démarrage.
Build React de production : `main.114d0ce5.js`.

Le reçu de migration de l'ancien moteur a été établi après vérification réelle
du chemin, de son empreinte `a8f52830e161699607cdcac44b5499c28456f71c2417a1977d91ef63c18210b5`
et de sa date de création FILETIME `134375215523394203`. Ce reçu local permet
le premier remplacement ; les lancements suivants sont attestés par la GUI.

Les anciens portables ne reçoivent pas rétroactivement cette protection.
Utiliser le nouveau livrable ; aucun essai d'écoute ne permet ici de déclarer
la qualité sonore corrigée.
