# Refonte de l’espace de travail TimoxVasio — 24 octobre 2026

## Intention et diagnostic

L’utilisateur demande de reprendre la GUI maintenant : onglets peu lisibles,
trop de contenu simultané, explications WASAPI et performances à isoler. Il cite
FlexLayout comme référence et Radix comme possibilité. Ce document traduit cette
demande en choix de conception ; les dimensions et composants sont des choix du
concepteur, pas des exigences attribuées à l’utilisateur.

Le problème principal est la hiérarchie : Clock affiche son cours avant les
réglages, Performance mélange instruments et explications, WASAPI empile
inventaire et ponts configurés, et les onglets ressemblent aux boutons d’action.
Le thème surcharge en outre App.css via ProfessionalTheme.css ; rajouter une
troisième couche d’exceptions rendrait la suite plus difficile à maintenir.
La navigation devient latérale seulement à 1280px, d’où deux barres horizontales
dans une fenêtre ordinaire plus étroite.

## Disposition retenue

Une fenêtre d’outil audio, avec une navigation globale discrète, un panneau de
travail dominant et des actions stables. Reprendre de FlexLayout la séparation
entre barre d’onglets, barre d’outils et contenu. Le déplacement libre des panneaux
n’est pas nécessaire pour corriger la surcharge ; aucun partage automatique de
l’écran. Si FlexLayout est installé, un seul groupe d’onglets constitue la
disposition par défaut et les fenêtres détachées restent désactivées initialement.

```text
Timox VASIO                                  Engine running
───────────────────────────────────────────────────────────
Audio setup   │ Audio setup                        Help
Routing       │ [ ASIO ][ Clock ][ WASAPI ]
Analysis      │ ──────────────────────────────────────────
Performance   │ Master audio clock                 Applied
API           │ Clock source     [ ASIO           ▾ ]
Logs          │ Driver             SSL ASIO Driver 1
              │ Sample rate      [ 48000 Hz       ▾ ]
              │ Block size       [ 256 frames     ▾ ]
              │ Block interval     5.33 ms
              │
              │                  [Reset driver…]
              │ ──────────────────────────────────────────
              │ Changes pending    Discard       Apply
```

Navigation latérale de 176px dès 960px ; sous ce seuil, barre horizontale compacte
avec défilement propre si nécessaire. Aucun contexte de câble, canal ou processus
dans cette navigation. Les détails du routage restent des dialogues issus des
menus contextuels ; sélectionner un câble n’ouvre rien.

Dans Setup, onglets joints au panneau, hauteur 36px, état sélectionné par surface
et trait de bord, sans l’apparence de trois gros boutons isolés. Alignement gauche
des titres et champs. Formulaire Clock limité à environ 640px, étiquettes alignées
et valeurs faciles à comparer. Barre Apply/Discard attachée au bas du panneau ;
le statut du brouillon est unique et change uniquement lors d’une vraie édition.

## Répartition du contenu

- **ASIO** : pilote physique et état en premier ; profils de canaux ensuite,
  tableau compact. Explication pilote virtuel/physique et affectation des profils
  dans l’aide. Une seule phrase opérationnelle lorsque la réouverture du client
  est nécessaire.
- **Clock** : source, pilote effectif, fréquence, bloc, intervalle calculé.
  Mention permanente brève « Master audio clock · not MIDI » ; exemples,
  stratégies et limites dans l’aide. Une modification qui coupe le son reste
  signalée près de l’action Apply ; cette conséquence ne doit pas être cachée.
- **WASAPI** : ponts configurés identifiables avant l’inventaire ; barre de
  recherche et filtre disponibles/indisponibles ; arborescence par périphérique,
  direction, endpoint, canaux. Groupes matériels repliés par défaut. Présenter
  les nombres comme endpoints, pas comme nombres de canaux. Les longs IDs
  techniques restent dans les détails. Conserver les actions Add/Configure/Remove.
- **Performance** : état du délai audio, marge, incidents puis charge par
  application et pertes transport. Mesures détaillées accessibles via une
  section repliable clairement nommée. Déplacer le cours sur le chemin du signal,
  les limites des mesures et la distinction CPU/callback dans l’aide. Ne pas
  supprimer les données ou les alertes pour alléger l’écran.
- **Routing** : graphe dominant et barre d’outils compacte ; explication des
  menus contextuels dans l’aide. Aucun panneau permanent de propriétés.

## Aide à la demande

Un bouton textuel « Help » dans l’en-tête de chaque espace ouvre un dialogue
d’aide avec le sujet courant. Rubriques : ASIO et profils, horloge audio,
fréquences et conversion WASAPI, périphériques indisponibles, interprétation
des performances. L’aide conserve les explications existantes mais les découpe
en paragraphes courts, sous-titres et exemples. Largeur de texte maximale 68ch.
Un lien contextualisé « Choosing a clock » peut ouvrir directement la rubrique.
Les avertissements bloquants, erreurs et conséquences immédiates restent visibles
avec les réglages ; l’aide ne sert pas de cache pour les problèmes.

Radix Dialog convient au focus contenu dans la fenêtre, à Escape et au retour du
focus. Radix Tabs convient aux onglets clavier. Aucun tooltip ne doit contenir
un manuel ou une information indispensable. Le dialogue d’aide est séparé du
brouillon audio et n’appelle jamais l’API de configuration.

## Style et composants

Palette proposée : fond #edf0f3, panneau #ffffff, barre #e4e9ee, bord #b9c3cd,
texte #202b36, accent bleu #245f85. Conserver couleurs danger/avertissement/activité
uniquement pour les états, pas pour décorer les catégories. Segoe UI Variable,
14px corps, 13px commandes, 18px titre de l’espace ; chiffres tabulaires pour les
mesures. Pas de labels décoratifs tout en capitales ni de bandes de couleur sur
chaque carte. Espacements 8/12/16/24px, contrôles 34–36px, rayons 3–4px.

Extraire les composants SetupTabs, WorkspaceHeader, HelpDialog et le contenu
d’aide hors d’App.js. Conserver l’état du brouillon dans App ; un changement
d’onglet ne doit ni le réinitialiser ni appliquer la configuration. Modifier les
règles du thème existant plutôt qu’empiler des surcharges contradictoires.

## Validation avant livraison

Contrôler visuellement ASIO, Clock, WASAPI, Performance et Routing à 1440×900
et 960×720, puis l’absence de débordement global à 760px. À 960×720, les contrôles
Clock et Apply doivent être accessibles sans traverser un bloc de documentation.
Vérifier clavier complet, focus visible, Escape et retour au déclencheur d’aide,
préservation du brouillon entre onglets, absence d’appel API en ouvrant l’aide,
états indisponibles/offline et longues désignations d’endpoints.
Conserver les tests existants de transaction et de routage ; adapter les tests
qui attendaient les explications dans le formulaire pour les vérifier dans l’aide.

Références consultées : [FlexLayout](https://github.com/caplin/FlexLayout),
[Radix Dialog](https://www.radix-ui.com/primitives/docs/components/dialog),
[Radix Tabs](https://www.radix-ui.com/primitives/docs/components/tabs).
