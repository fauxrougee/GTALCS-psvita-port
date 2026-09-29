# Compilation PS Vita

## Prérequis

Git, CMake 3.20+, Ninja, Python 3.10+, FFmpeg/FFprobe dans `PATH`, les paquets
de `tools/vita/requirements.txt` et [VitaSDK](https://vitasdk.org/).
Installer ces bibliothèques cible avec le gestionnaire officiel vdpm :

```text
zlib taihen kubridge libmathneon vitaShaRK SceShaccCgExt vitaGL openal-soft mpg123
```

La préparation locale utilise GCC 15.2.0, les outils Windows du SDK du
26/09/2026 et les bibliothèques cible de la 01.13 testée sur console.
Leurs empreintes sont dans `toolchain-reference.json`, comme référence de
diagnostic. Le SDK et les modules système Sony ne sont pas distribués ici.

## Windows natif

Installer VitaSDK avec le [bootstrap PowerShell officiel](https://github.com/vitasdk/vdpm),
puis ses bibliothèques via `vdpm.exe install` et la liste ci-dessus.

```powershell
$env:VITASDK = 'C:/chemin/vers/vitasdk'
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r tools/vita/requirements.txt
.venv/Scripts/python.exe tools/vita/build.py --jobs 4
```

CMake et Ninja sont recherchés dans `PATH`, puis dans les installations
Visual Studio disponibles. GCC VitaSDK compile le jeu ; MSVC sert seulement
aux tests hôtes. `tools/vita/build-windows.ps1` est un raccourci et ne lance
jamais WSL. Sans `VITASDK`, le SDK local est recherché à `sdk/windows/vitasdk`.
On peut aussi utiliser `--sdk C:/chemin/vers/vitasdk`.

## Linux

Installer les prérequis hôtes avec le gestionnaire de la distribution,
puis VitaSDK et les bibliothèques cible avec [vdpm](https://github.com/vitasdk/vdpm).

```sh
export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/vita/requirements.txt
.venv/bin/python tools/vita/build.py --jobs 4
```

`tools/vita/install-vitasdk.sh` peut préparer un SDK séparé sur Linux. Il
n'efface aucun ancien répertoire et ne modifie pas les profils shell.
WSL 1 est exclu des scripts de préparation.

## Options et résultats

```sh
python tools/vita/build.py --no-intro
python tools/vita/build.py --sdk /chemin/sdk --build-dir /chemin/build --output-dir /chemin/dist --jobs 4
```

Sans option, `build/vita-intro/` contient le jeu, le lecteur, leurs SELF et
la vidéo préparée. `dist/reLCS-01.15-intro-complete.vpk` contient :

- `eboot.bin` : lecteur compilé depuis `vita/launcher/` ;
- `game.bin` : moteur compilé depuis les sources actuelles ;
- `boot/intro.vtm` : toutes les images et le son PCM ;
- l'écran de chargement, l'icône, la LiveArea et la licence LZ4.

`--no-intro` construit dans `build/vita-no-intro/` et produit un VPK léger.
Les sources du moteur sont identiques dans les deux variantes. Conserver les
ELF correspondant au VPK installé pour analyser un éventuel dump.

CMake peut aussi être utilisé directement :

```sh
cmake -S vita -B build/vita-intro -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita-intro --parallel 4
```

CMake doit trouver le Python contenant les dépendances ; utiliser
`-DPython3_EXECUTABLE=/chemin/python` si nécessaire. Le patch librw est appliqué
une seule fois avant compilation. Une modification incompatible du sous-module
provoque une erreur explicite.

## Vidéo

Le MP4 fourni est déjà édité : aucune police Windows ou vidéo d'origine n'est
requise. `pack-boot-movie.py` décode les 2 774 images et le son, puis les
compresse sans perte supplémentaire. Le VTM d'environ 187 Mo est généré dans
le dossier de build et exclu de Git.

Le paquet vérifie les exécutables, l'attribut de mémoire étendue, les
illustrations et la vidéo. Voir [les tests](../docs/DEVELOPMENT.md).
