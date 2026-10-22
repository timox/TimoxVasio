# VM Windows KVM pour tester TimoxVasio

Date : 22 octobre 2026.

Cette VM, hébergée sur une autre machine, servira à tester le futur pilote audio Windows natif et son pont avec le graphe TimoxVasio. Elle permettra de vérifier installation, démarrage, routage, retrait et stabilité. Les mesures finales de latence et de performances ASIO demanderont ensuite du matériel physique.

## Configuration recommandée

| Élément | Réglage |
|---|---|
| Hyperviseur | KVM/QEMU, administré avec libvirt/virt-manager ou interface équivalente |
| Architecture | x86_64 |
| Machine | Q35 |
| CPU | 4 vCPU, mode `host-passthrough` sur hôte x86_64 compatible |
| Mémoire | 8 Go fixes |
| Disque | 100 Go virtuels, format qcow2, stockage SSD ; prévoir aussi la place des sauvegardes |
| Contrôleur disque | SATA pour installer sans pilote supplémentaire ; VirtIO possible avec ses pilotes Windows |
| Firmware | UEFI / OVMF, avec stockage NVRAM propre à la VM |
| TPM | TPM 2.0 émulé avec `swtpm`, état propre à la VM |
| Secure Boot | Activé pour l'installation initiale ; modification ultérieure uniquement dans cette VM pour le pilote de laboratoire |
| Réseau | Adresse stable et joignable depuis le poste de développement, par bridge LAN ou réseau routé/VPN |
| Console | SPICE ou VNC via l'hyperviseur, utilisable même lorsque Windows ne démarre plus |
| Audio | Intel HD Audio **ICH9** (`ich9`), avec backend SPICE si la console utilise SPICE ; aucun passthrough USB/PCI nécessaire pour les premiers essais |

Les valeurs CPU, mémoire et disque sont nos recommandations de travail. Les prérequis Microsoft comprennent notamment UEFI, TPM 2.0, au moins 4 Go de RAM et 64 Go de stockage.

ISO fourni :

```text
26100.1742.240906-0331.ge_release_svc_refresh_CLIENT_LTSC_EVAL_x64FRE_fr-fr.iso
```

Il se trouve actuellement dans `C:\Users\timo\Downloads` sur le poste de développement. Il s'agit de l'image Windows 11 Enterprise LTSC Evaluation prévue pour ce banc.

## Installation initiale

1. Créer la VM et installer Windows depuis l'ISO. Choisir **Windows 11 Enterprise LTSC Evaluation sans N** pour ce banc audio.
2. Installer les pilotes réseau/stockage nécessaires et les mises à jour Windows.
3. Créer un compte local dédié, par exemple `timoxlab`, administrateur pour les opérations d'installation du pilote.
4. Vérifier le réseau et conserver l'accès à la console de l'hyperviseur.
5. Préparer OpenSSH Server dans Windows pour l'accès décrit ci-dessous.
6. Arrêter Windows proprement puis créer une sauvegarde de référence avant toute installation du pilote Timox.

La sauvegarde doit préserver ensemble le disque, la configuration de la VM, sa NVRAM UEFI et l'état du TPM émulé. Un snapshot du disque seul peut être insuffisant. Utiliser la méthode de sauvegarde cohérente proposée par l'hyperviseur et vérifier la possibilité de restauration.

Nom suggéré : `windows-propre-avant-pilote-timox`.

## Accès prévu depuis le poste de développement

L'accès principal sera **SSH directement dans Windows**, pour transférer les binaires, exécuter les tests et récupérer les journaux.

- Installer et démarrer le composant Windows **OpenSSH Server**.
- Autoriser TCP 22 depuis le poste de développement ou le réseau VPN prévu.
- Utiliser une authentification par clé publique ; la clé publique à autoriser sera fournie lors de la préparation de l'accès.
- Communiquer l'adresse IP ou le nom DNS, le port SSH si différent de 22 et le nom du compte.
- Ne pas transmettre de mot de passe ni de clé privée dans la conversation.

L'adresse doit être réellement joignable depuis le poste de développement. Avec un NAT libvirt, prévoir une route ou un tunnel adapté ; la seule adresse privée de la VM ne garantit pas cet accès.

L'utilisateur conserve initialement le contrôle de la console, des redémarrages forcés et des restaurations. Aucun accès administrateur à l'hôte Linux n'est nécessaire pour commencer. Si Windows plante, SSH peut devenir indisponible : la console de l'hyperviseur reste le moyen de récupération.

Les essais audio interactifs se feront dans la session console Windows. SSH sert à l'administration ; il ne remplace pas le contexte d'une session utilisateur interactive. Éviter de substituer une session RDP avec redirection audio au banc console sans le noter dans le protocole.

## Préparation ultérieure du pilote de laboratoire

Attendre le paquet de test et les instructions vérifiées avant cette étape. Le pilote natif n'est pas encore qualifié pour une utilisation quotidienne.

Le chargement du paquet signé avec un certificat de laboratoire nécessitera la configuration de signature de test Windows et la désactivation de Secure Boot **dans cette VM uniquement**. La signature de test ne dispense pas de signer le binaire. Ces réglages et les certificats seront préparés après la sauvegarde de référence ; aucun changement de sécurité du poste audio ou de l'hôte Linux n'est demandé.

Les premiers essais vérifieront l'installation et le retrait, les endpoints Windows, le passage PCM dans les deux sens et la récupération après incident. Le contrôle de l'isolation entre sessions dispose d'un protocole séparé : [qualification des sessions natives](native-audio-session-qualification.md).

## Informations à transmettre quand la VM est prête

- Hyperviseur/interface utilisée, par exemple libvirt + virt-manager ou Proxmox.
- Adresse de la VM et compte SSH prévu.
- Édition et numéro de build Windows installés (`winver`).
- Confirmation de la sauvegarde avant pilote et de l'accès console.

La connexion et les essais commenceront après le signal de disponibilité de l'utilisateur.

## Références

- [Prérequis Windows 11 — Microsoft](https://learn.microsoft.com/en-us/windows/whats-new/windows-11-requirements).
- [Configuration des domaines et TPM émulé — libvirt](https://libvirt.org/formatdomain.html).
- [Secure Boot — libvirt](https://www.libvirt.org/kbase/secureboot.html).
- [Installation d'OpenSSH Server — Microsoft](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_install_firstuse?pivots=windows-11).
- [Chargement des pilotes signés pour les tests — Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).
