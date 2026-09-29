# Vérification de la 01.15

La 01.15 retire le panneau FPS/CPU/RAM de l'écran. La fonction de dessin,
son appel et sa déclaration sont supprimés. Les mesures nécessaires aux
rapports de diagnostic restent actives.

Vérifications locales avec les outils Windows natifs :

- Compilation du jeu et création du VPK avec l'intro complète réussies.
- Fonction d'affichage absente des symboles du jeu compilé ; fonctions de
  collecte des mesures toujours présentes.
- Lecteur d'intro, vidéo avec son, écran de chargement, icône et LiveArea
  identiques à ceux de la 01.14, par comparaison SHA-256 des fichiers empaquetés.
- Métadonnées 01.15, mémoire étendue, contenu du VPK, CRC ZIP et segments
  des ELF vérifiés par les outils de compilation.

Les [vérifications complètes de l'intro en 01.14](VALIDATION-01.14.md)
restent applicables à ces fichiers inchangés. Aucun nouvel essai sur une
PS Vita physique n'a été effectué pendant cette modification.
