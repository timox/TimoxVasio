# Accès SSH à la VM Windows de test

Exécuter cette préparation **dans la VM dédiée**, depuis sa console et dans
Windows PowerShell 5.1 x64 ouvert comme administrateur. Garder la console de
récupération et un snapshot avant modification. Ne pas lancer le script sur
l'hôte. Aucun pilote, certificat de signature ou réglage TESTSIGNING n'est installé.

Le compte doit déjà exister, être activé et membre direct des Administrateurs
locaux. Le script ne crée ni compte ni mot de passe. Choisir un nom simple,
sans domaine, espace ou caractère spécial SSH.

## Préparation

Copier dans la VM [Configure-TimoxTestVmSsh.ps1](scripts/Configure-TimoxTestVmSsh.ps1)
et [timox-vm.pub](scripts/timox-vm.pub), la clé publique Ed25519 dédiée à ce banc.
Ne jamais transférer la clé privée dans la VM ni la communiquer. Elle reste sur
le poste de développement dans un dossier protégé et ignoré par Git.

Empreinte de cette clé publique client :
`SHA256:XbE/Z60lEJtvikBYm+yoH/OcLGKckeMMI0q5/fukdJI`.
Elle est distincte de la clé d'hôte de la VM, qui sera relevée après configuration.

Adapter ces trois valeurs à la VM et au poste qui établira la connexion :

```powershell
$account = 'timo'
$publicKey = 'C:\Users\timo\Downloads\timox-vm.pub'
$sourceAddress = '192.168.1.50' # IP du client vue par la VM, pas IP de la VM
```

Un CIDR précis est accepté si nécessaire ; `Any`, les noms DNS et les préfixes
`/0` sont refusés. Avec NAT, utiliser l'adresse réellement vue par la VM et
configurer la redirection de port dans l'hyperviseur séparément si nécessaire.

Depuis le dossier contenant le script :

```powershell
.\Configure-TimoxTestVmSsh.ps1 -UserName $account -PublicKeyPath $publicKey -AllowedRemoteAddress $sourceAddress -WhatIf
.\Configure-TimoxTestVmSsh.ps1 -UserName $account -PublicKeyPath $publicKey -AllowedRemoteAddress $sourceAddress
```

La seconde commande demande confirmation. `-Port 2222` permet un autre port.
`-WhatIf` valide les entrées et consulte les comptes/règles, mais n'installe rien,
ne crée aucun fichier et ne modifie ni service ni pare-feu. L'installation de la
capacité peut nécessiter Windows Update ou une source Features on Demand ; un
échec est rapporté sans téléchargement alternatif automatique.

## Changements effectués

- Installation de la capacité OpenSSH Server si absente ; service `sshd` automatique.
- Remplacement de `C:\ProgramData\ssh\sshd_config` par une configuration dédiée :
  un seul `AllowUsers`, clé publique obligatoire, mots de passe et authentification
  interactive désactivés, SFTP disponible, forwarding désactivé.
- Remplacement de `administrators_authorized_keys` par la seule clé fournie.
  Les anciens fichiers et leurs ACL sont sauvegardés dans un dossier
  `Timox-backup-<GUID>` sous `C:\ProgramData\ssh`. Les ACL des nouveaux fichiers
  autorisent uniquement SYSTEM et Administrateurs, identifiés par leurs SID.
- Génération des clés d'hôte manquantes avec `ssh-keygen -A`, puis validation
  `sshd -t` avant démarrage.
- Règle `Timox-TestVM-SSH` limitée au programme, au port et aux sources demandées.
  Seule la règle standard `OpenSSH-Server-In-TCP` est désactivée ; aucune autre
  règle existante n'est modifiée. **Une autre règle permissive déjà présente peut
  toujours ouvrir l'accès** : examiner le pare-feu de la VM et du réseau.

Ce script remplace volontairement les réglages SSH de cette VM de laboratoire.
Il ne fusionne pas une configuration SSH de production. Il refuse une règle
`Timox-TestVM-SSH` préexistante : avant une nouvelle exécution, inspecter puis
retirer explicitement cette règle depuis la console de la VM.

En cas d'échec, les fichiers sauvegardés et leurs ACL sont restaurés, les fichiers
nouvellement créés par la transaction supprimés, et `sshd` reste arrêté. La règle
projet est désactivée. La capacité, les clés d'hôte et les sauvegardes peuvent
rester ; après un échec tardif, le démarrage automatique du service peut aussi
rester configuré malgré son arrêt immédiat. La règle standard n'est pas réactivée automatiquement. Toute erreur de
restauration est affichée. Reprendre depuis la console ou restaurer le snapshot.
Un arrêt brutal pendant l'opération exige une récupération manuelle depuis les
sauvegardes ; le script n'est pas un installateur transactionnel résistant aux crashs.

## Connexion et informations à communiquer

Relever l'IP de la VM avec `Get-NetIPAddress`, puis communiquer **IP, port,
nom du compte et empreinte publique de la clé d'hôte** affichée par le script.
Ne transmettre aucun mot de passe, clé privée ou jeton. Le poste client pourra
ensuite tester, en remplaçant l'adresse et le chemin de sa clé privée locale :

```powershell
ssh -o IdentitiesOnly=yes -i C:\chemin\cle-privee -p 22 timo@192.168.1.60
sftp -o IdentitiesOnly=yes -i C:\chemin\cle-privee -P 22 timo@192.168.1.60
```

Comparer l'empreinte affichée à la première connexion avec celle relevée dans
la console VM. Ne pas désactiver la vérification de clé d'hôte.

## Sources et validation

[Configuration OpenSSH Windows](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh-server-configuration)
et [gestion des clés Windows](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_keymanagement)
documentent les comptes autorisés et le fichier des administrateurs.

La validation locale de ce lot porte sur le parsing PowerShell et des entrées
invalides sans mutation. L'installation réelle, le téléchargement de la capacité,
le redémarrage de `sshd`, SSH/SFTP et les restrictions réseau restent à vérifier
dans la VM autorisée. Aucune configuration SSH de l'hôte n'a été modifiée.
