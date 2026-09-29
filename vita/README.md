# Building for PS Vita

## Prerequisites

Install Git, CMake 3.20+, Ninja, Python 3.10+, FFmpeg/FFprobe in `PATH`, the
packages in `tools/vita/requirements.txt`, and [VitaSDK](https://vitasdk.org/).
Install these target libraries with the official vdpm package manager:

```text
zlib taihen kubridge libmathneon vitaShaRK SceShaccCgExt vitaGL openal-soft mpg123
```

The locally validated build uses GCC 15.2.0, the Windows SDK tools dated
2026-09-26, and the target libraries used by the hardware-tested 01.13 release.
Their hashes are recorded in `toolchain-reference.json` for diagnostics.
The SDK and Sony system modules are not distributed here.

## Native Windows

Install VitaSDK using the [official PowerShell bootstrap](https://github.com/vitasdk/vdpm),
then install the libraries listed above with `vdpm.exe install`.

```powershell
$env:VITASDK = 'C:/path/to/vitasdk'
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r tools/vita/requirements.txt
.venv/Scripts/python.exe tools/vita/build.py --jobs 4
```

CMake and Ninja are located through `PATH`, then through available Visual
Studio installations. VitaSDK GCC builds the game; MSVC is used only for host
tests. `tools/vita/build-windows.ps1` is a shortcut and never launches WSL.
Without `VITASDK`, the script looks for a local SDK at `sdk/windows/vitasdk`.
You can also pass `--sdk C:/path/to/vitasdk`.

## Linux

Install the host prerequisites with your distribution's package manager,
then install VitaSDK and the target libraries using [vdpm](https://github.com/vitasdk/vdpm).

```sh
export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/vita/requirements.txt
.venv/bin/python tools/vita/build.py --jobs 4
```

`tools/vita/install-vitasdk.sh` can set up a separate SDK on Linux. It does
not delete existing directories or change shell profiles. The setup scripts
do not support WSL 1.

## Options and outputs

```sh
python tools/vita/build.py --no-intro
python tools/vita/build.py --sdk /path/to/sdk --build-dir /path/to/build --output-dir /path/to/dist --jobs 4
```

By default, `build/vita-intro/` contains the game, intro player, their SELF
executables, and the prepared video. `dist/reLCS-01.15-intro-complete.vpk` contains:

- `eboot.bin`: intro player built from `vita/launcher/`;
- `game.bin`: engine built from the current sources;
- `boot/intro.vtm`: all video frames and PCM audio;
- the loading screen, icon, LiveArea, and LZ4 license.

`--no-intro` builds in `build/vita-no-intro/` and produces a small VPK.
Both variants use the same engine sources. Keep the ELF files matching the
installed VPK when investigating a crash dump.

You can also use CMake directly:

```sh
cmake -S vita -B build/vita-intro -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita-intro --parallel 4
```

CMake must find the Python interpreter with the required packages. Set
`-DPython3_EXECUTABLE=/path/to/python` if needed. The librw patch is applied
once before compilation. Incompatible changes to the dependency produce
an explicit error.

## Video

The supplied MP4 is already edited: no Windows fonts or original source video
are required. `pack-boot-movie.py` decodes all 2,774 frames and the audio, then
compresses them without additional quality loss. The approximately 187 MB VTM
is generated in the build directory and excluded from Git.

Packaging verifies the executables, extended memory attribute, artwork, and
video. See the [testing guide](../docs/DEVELOPMENT.md).
