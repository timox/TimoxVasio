# Préparer les essais Linux depuis la GUI

Dans **Routing**, clic droit sur le fond du graphe, puis **Configure PWAR / PipeWire…**.

1. Saisir l'adresse IPv4 de Linux et son port UDP de réception (8321 par défaut).
2. Cliquer **Save network settings**. Le moteur écrit son fichier utilisateur
   `pwarASIO.cfg`, conserve les autres réglages et ne démarre pas l'audio.
3. Si PWAR est installé et découvert, utiliser **Select PWAR**, fermer la fenêtre
   puis **Apply configuration**. Résoudre les éventuelles connexions incompatibles
   signalées par la GUI avant l'application.
4. Si PWAR est déjà utilisé, enregistrer les réglages puis cliquer
   **Reopen PWAR driver**. Cette action interrompt l'audio. Les modifications du
   graphe doivent d'abord être appliquées ou abandonnées.
5. Sur Linux, configurer PWAR pour envoyer vers l'adresse Windows, port UDP 8321.
   Lancer une source sonore et observer les vumètres des canaux dans le graphe.

La fenêtre distingue l'état du pilote de la réception réseau : un pilote ouvert
ne prouve pas que Linux envoie des paquets ou que le son arrive. Les erreurs HTTP
et de réouverture sont affichées dans la fenêtre. Le port de destination modifiable
nécessite le correctif PWAR 0004 fourni dans `patches/pwar` ; les anciennes versions
utilisent toujours 8321. Aucun pilote PWAR n'est installé automatiquement.

L'API locale `GET /api/v1/pwar` retourne `address`, `port`, `exists`.
`PUT /api/v1/pwar` accepte exactement `address` (IPv4) et `port` (entier 1–65535),
et retourne `reopenRequired: true` après enregistrement. Les données invalides
retournent HTTP 400 ; les erreurs de fichier HTTP 500. Les chemins de fichier ne
peuvent pas être fournis par le client. Voir `api/openapi-v1.json`.

Les tests automatiques utilisent un profil temporaire et ne modifient pas le
fichier PWAR de l'utilisateur. Les essais audio réels entre Windows et Linux
restent à effectuer ; la compilation de la GUI ne met pas à jour un ancien
installateur déjà distribué.
