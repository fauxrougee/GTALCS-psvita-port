# Tests, sources et publication

## Vérifications avant une release

```sh
python tools/vita/check-perf-hooks.py
python tools/vita/check-perf-stats.py
python tools/vita/check-render-cache.py
python tools/vita/check-async-log.py
python tools/vita/build.py
python tools/vita/check-software-movie.py
python tools/vita/check-software-playback.py
```

Les tests hôtes utilisent GCC avec ASan/UBSan sur Linux, ou MSVC avec ASan
sur Windows. Sur Windows, installer les outils C++ x64 et AddressSanitizer
(validation locale : MSVC 14.44). `VITA_TEST_VCVARS` et
`VITA_TEST_MSVC_VERSION` permettent de choisir une autre installation.
Ils ne servent pas à compiler le jeu ARM.

Les tests du lecteur vérifient les images, le son, les fichiers endommagés,
la file vidéo et la transition vers le jeu. Les appels système Vita sont
simulés ; ils ne remplacent pas un essai sur console. Le test complet de
lecture dure environ 111 secondes, comme la vidéo.

Pour tester le décodeur ARM lié dans la release :

```sh
python -m pip install unicorn
python tools/vita/check-movie-arm.py --limit 16
# Sans --limit : toutes les images, plus long.
```

Les FPS se mesurent sur la Vita ; les tests hôtes ne les prédisent pas.
Le log de partie est remplacé à chaque lancement. Conserver `intro.log`
pour un problème de lecteur et les ELF exacts du VPK installé.

## Préparer les sources

```sh
python tools/vita/export-source.py
```

L'archive dans `dist/` contient les sources actuelles, les notices, les
dépendances à leurs révisions enregistrées et la vidéo éditée. Le patch Vita
complet est inclus et appliqué par CMake à librw. Aucun SDK, ancien VPK, dump,
log, sauvegarde, donnée de partie ou chemin personnel n'est inclus.
Elle peut être extraite dans un nouveau dossier et compilée avec un SDK
installé, sans accéder à l'espace de travail d'origine.

Pour publier le dépôt Git existant, conserver les sous-modules et le patch.
Pour publier l'archive via un nouveau dépôt, les dépendances sont déjà
intégrées : aucune métadonnée `.git` n'est incluse dans le ZIP.
Ne pas ajouter `assets/`, `sdk/`, `build/`, `dist/` ou `.local/` aux sources.

Dans le dossier extrait, créer un dépôt Git et envoyer ces sources vers un
nouveau dépôt GitHub vide, en remplaçant l'URL de l'exemple :

```sh
git init -b main
git add .
git commit -m "Prepare reLCS PS Vita 01.15 with full intro"
git remote add origin https://github.com/VOTRE-COMPTE/VOTRE-DEPOT.git
git push -u origin main
```

La vidéo MP4 fait environ 11 Mo et est suivie normalement par Git, sans LFS.
Seul le MP4 est versionné, pas le gros VTM généré. Le VPK et les symboles vont
dans les **Releases**, pas dans l'historique des sources. Les workflows
produisent des artefacts sans publier automatiquement de release ou de tag.
