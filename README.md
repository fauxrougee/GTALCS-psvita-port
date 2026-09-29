# GTA Liberty City Stories — PS Vita

Port natif de GTA Liberty City Stories pour PS Vita, préparé par **fauxrouge**,
à partir de [reStories](https://github.com/knackers4/res) et
[librw](https://github.com/aap/librw).

La version **01.15** conserve le moteur de la 01.13 validée sur console et
l'intro complète avec son audio. La compilation inclut les cartons
« fauxrouge present » et « a psvita port », l'icône et la LiveArea avec START.

- Résolution native 960 × 544 ; fréquences demandées CPU 444 / GPU 222 MHz.
- Limiteur logiciel à 30 FPS retiré ; synchronisation de l'écran conservée.
- Caches d'éclairage et de reflets, cache de shaders, journal asynchrone.
- Affichage FPS/CPU/RAM retiré de l'écran ; rapports de performances dans
  `ux0:data/reLCS/log.txt`.

Les performances dépendent de la scène : **45–60 FPS constants ne sont pas
garantis**. La résolution, les textures, les distances et les effets n'ont pas
été réduits pour le débridage.

## Installation

Installer le VPK sur une PS Vita équipée d'un environnement homebrew. Placer
les données LCS converties depuis sa propre version PS2 dans
`ux0:data/reLCS/` (notamment `models/gta3.img`). Le dépôt contient le moteur et
la vidéo d'intro modifiée ; il ne contient pas les données nécessaires à la
partie, les sauvegardes ou les modules système Sony.

Le compilateur de shaders `libshacccg.suprx` doit être présent sur la console,
à `ur0:data/libshacccg.suprx` ou `ur0:data/external/libshacccg.suprx`.
Les informations du projet d'origine et du convertisseur sont conservées
dans [docs/UPSTREAM.md](docs/UPSTREAM.md).

## Compiler

La compilation fonctionne avec **VitaSDK, CMake, Ninja, Python 3.10+ et FFmpeg**.
Les bibliothèques cible et les instructions Windows/Linux sont détaillées dans
[vita/README.md](vita/README.md).

Après installation des prérequis :

```sh
python -m pip install -r tools/vita/requirements.txt
python tools/vita/build.py --jobs 4
```

L'intro complète et son audio sont inclus par défaut. Le MP4 modifié est fourni
à `vita/boot/intro.mp4` ; son conteneur de lecture est généré automatiquement.
Aucun ancien VPK, exécutable précompilé ou dossier personnel n'est nécessaire.

Les résultats se trouvent dans `dist/` : VPK, symboles du jeu et du lecteur
séparés, manifeste de vérification et sommes SHA-256.

```sh
# Petit VPK pour tester le jeu sans l'intro :
python tools/vita/build.py --no-intro --jobs 4
```

Sous Windows, le script utilise les outils natifs et ne lance pas WSL.
Pour un clonage Git, initialiser les dépendances avec
`git submodule update --init --recursive`. L'archive source exportée contient
déjà les dépendances ; cette étape n'est pas nécessaire avec cette archive.

## Organisation et contribution

- `src/` : moteur et adaptation Vita ; la 01.15 retire l'affichage des compteurs.
- `vendor/` : dépendances d'origine ; le patch Vita de librw est appliqué par CMake.
- `vita/launcher/` : lecteur d'intro isolé, qui lance ensuite le jeu.
- `vita/boot/intro.mp4` : vidéo éditée ; les fichiers intermédiaires ne sont pas versionnés.
- `vita/sce_sys/` : icône et LiveArea.
- `tools/vita/` : compilation, conversion, tests et export des sources.
- `tools/frenchfix/` : complément des textes de menu français, facultatif.

Voir [les vérifications de cette version](docs/VALIDATION-01.15.md),
[les tests et la publication](docs/DEVELOPMENT.md) et
[les crédits et licences](docs/CREDITS.md). Le workflow Vita construit un
artefact ; il ne publie pas de release et ne déplace aucun tag automatiquement.
