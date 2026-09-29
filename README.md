# GTA Liberty City Stories - PS Vita

A native GTA Liberty City Stories port for PS Vita, prepared by **fauxrouge**,
based on [reStories](https://github.com/knackers4/res) and
[librw](https://github.com/aap/librw).

Version **01.15** keeps the engine from the hardware-tested 01.13 release and
the full intro with audio. The build includes the "fauxrouge present" and
"a psvita port" title cards, the custom icon, and the LiveArea with START.

- Native 960 x 544 resolution; requested clocks: CPU 444 / GPU 222 MHz.
- The software 30 FPS limiter is removed; display synchronization is retained.
- Lighting and reflection caches, shader caching, and asynchronous logging.
- The FPS/CPU/RAM overlay is removed; performance reports are written to
  `ux0:data/reLCS/log.txt`.

Performance varies by scene: **a constant 45-60 FPS is not guaranteed**.
Resolution, textures, draw distances, and effects were not reduced to remove
the frame rate cap.

## Installation

Install the VPK on a homebrew-enabled PS Vita. Place LCS data converted from
your own PS2 copy in `ux0:data/reLCS/`, including `models/gta3.img`. This
repository includes the engine and edited intro video. Gameplay data, save
files, and Sony system modules are not included.

The `libshacccg.suprx` shader compiler must be installed on the console at
`ur0:data/libshacccg.suprx` or `ur0:data/external/libshacccg.suprx`.
The original project's information and asset converter link are preserved
in [docs/UPSTREAM.md](docs/UPSTREAM.md).

## Building

The build requires **VitaSDK, CMake, Ninja, Python 3.10+, and FFmpeg**.
See [vita/README.md](vita/README.md) for target libraries and Windows/Linux
setup instructions.

Once the prerequisites are installed:

```sh
python -m pip install -r tools/vita/requirements.txt
python tools/vita/build.py --jobs 4
```

The full intro and its audio are included by default. The edited MP4 is
provided at `vita/boot/intro.mp4`; its playback container is generated
automatically. No older VPK, prebuilt executable, or personal folder is needed.

Build outputs are placed in `dist/`: the VPK, separate game and intro player
symbols, a verification manifest, and SHA-256 checksums.

```sh
# Build a small VPK for testing the game without the intro:
python tools/vita/build.py --no-intro --jobs 4
```

On Windows, the script uses native tools and does not launch WSL.
This repository includes its dependencies in `vendor/`, so no submodule
initialization is needed. When working from an upstream checkout that uses
submodules, run `git submodule update --init --recursive` first.

## Repository layout and contributing

- `src/`: engine and Vita adaptation; 01.15 removes the performance overlay.
- `vendor/`: upstream dependencies; CMake applies the Vita patch to librw.
- `vita/launcher/`: isolated intro player that launches the game afterward.
- `vita/boot/intro.mp4`: edited video; generated intermediate files are not tracked.
- `vita/sce_sys/`: icon and LiveArea assets.
- `tools/vita/`: build, conversion, validation, and source export tools.
- `tools/frenchfix/`: optional fixes for missing French menu text.

See the [release validation notes](docs/VALIDATION-01.15.md),
[testing and publishing guide](docs/DEVELOPMENT.md), and
[credits and license notices](docs/CREDITS.md). The Vita workflow produces
build artifacts; it does not publish releases or move tags automatically.
