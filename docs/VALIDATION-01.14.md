# Vérification de la 01.14

Validation locale du 29 septembre 2026, avec les outils Windows natifs.

- Le moteur conserve le comportement de la 01.13 validée sur console. La
  préparation de cette version modifie la compilation, la distribution et
  la documentation ; seules des références de commentaires changent dans
  les sources du moteur.
- Le jeu et le lecteur d'intro sont compilés depuis les sources. Une seconde
  compilation complète réussit depuis l'archive extraite dans un dossier
  neuf contenant des espaces, sans dépôt Git, ancien VPK ni ancien exécutable.
- L'option `--no-intro` compile et produit un paquet de lancement direct ;
  le retour à la compilation avec intro est également vérifié.
- Le conteneur régénéré depuis le MP4 inclus est identique octet pour octet
  à l'intro complète déjà fonctionnelle : 2 774 images et 5 326 080
  échantillons stéréo, soit environ 111 secondes.
- Le lecteur hôte vérifie toutes les images et tout le PCM, les recherches
  dans la vidéo, la file concurrente et le rejet de fichiers endommagés.
- Les fonctions ARM réellement liées dans le lecteur sont exécutées avec
  Unicorn sur les 2 774 images : CRC, décompression LZ4, couleurs et limites
  des tampons conformes.
- La simulation de lecture complète et les scénarios d'interruption,
  d'échec audio/vidéo, de synchronisation et de vidéo absente passent. Le
  système Vita y est simulé ; les éventuelles images devenues trop anciennes
  selon l'horloge hôte sont comptabilisées séparément.
- Les tests de comptage des performances, de conservation des valeurs du
  rendu et du journal asynchrone passent avec AddressSanitizer.
- Le VPK est contrôlé : CRC ZIP, empreintes des exécutables et des médias,
  métadonnées, mémoire étendue, icône et LiveArea. Les segments des ELF ARM
  du jeu et du lecteur ne se chevauchent pas.

Cette préparation ne mesure pas les FPS sur console. Le VPK 01.14 doit
encore être essayé sur une PS Vita réelle ; le workflow GitHub Actions
fourni n'a pas été exécuté sur GitHub pendant cette validation locale.
