# Feuille de route après la release 1.1.0

## Diagnostic de la taille du tampon ASIO

Déterminer si la taille de tampon configurée convient à la fréquence choisie et à la charge réelle, sans modifier automatiquement la configuration audio.

- Mesurer les échéances manquées du callback du pilote ASIO physique, sa durée de traitement et sa marge par rapport à la durée d’un bloc.
- Distinguer ces retards des sous-alimentations et débordements déjà comptés dans le transport entre les applications et le moteur.
- Publier les compteurs et leur période d’observation dans l’API documentée, puis les afficher dans les diagnostics de Timox VASIO Control.
- Signaler une taille de tampon possiblement trop faible uniquement après plusieurs mesures cohérentes, avec les valeurs observées et la fréquence utilisée.
- Vérifier avec plusieurs pilotes physiques et plusieurs applications que les compteurs reflètent des interruptions audibles et qu’une augmentation du tampon réduit effectivement les incidents.

Cette fonctionnalité est prévue **après** la finalisation de la release 1.1.0.
