# Build from source

Looking to install the game? Use the [installation guide](../README.md#install). You only need the instructions below if you want to compile the port.

## Get the source

```sh
git clone https://github.com/fauxrougee/GTALCS-psvita-port.git
cd GTALCS-psvita-port
```

Dependencies are included in `vendor/`. CMake applies `vita/librw-psp2.patch` before building.

## Prerequisites

Install Git, CMake 3.20+, Ninja, Python 3.10+, FFmpeg/FFprobe in `PATH`, the
packages in `tools/vita/requirements.txt`, and [VitaSDK](https://vitasdk.org/).
Install these target libraries with the official vdpm package manager:

```text
zlib taihen kubridge libmathneon vitaShaRK SceShaccCgExt vitaGL openal-soft mpg123
```

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

`tools/vita/install-vitasdk.sh` can handle SDK setup on Linux. WSL 1 is not
supported; use the native Windows tools instead.

## Build options

Use the Python from your virtual environment when running these commands:

```sh
python tools/vita/build.py --no-intro
python tools/vita/build.py --sdk /path/to/sdk --build-dir /path/to/build --output-dir /path/to/dist --jobs 4
```

The full build writes `dist/reLCS-01.15-intro-complete.vpk`.
`--no-intro` produces the smaller `dist/reLCS-01.15-no-intro.vpk` for testing.
Both builds also export ELF files and checksums. Keep the ELF files matching
the installed VPK when investigating a crash dump.

<details>
<summary>Using CMake directly</summary>

```sh
cmake -S vita -B build/vita-intro -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita-intro --parallel 4
```

CMake must find the Python interpreter with the required packages. Set
`-DPython3_EXECUTABLE=/path/to/python` if needed. The librw patch is applied
once before compilation. Incompatible changes to the dependency produce
an explicit error.

</details>

## Intro video

The full intro is included by default. The source is `vita/boot/intro.mp4`;
`pack-boot-movie.py` converts its 2,774 frames and audio to the playback
format without further quality loss. The generated VTM is roughly 187 MB
and stays in the build directory.

## Toolchain reference

The local 01.15 build used GCC 15.2.0, the Windows SDK tools from September 26,
2026, and the target libraries used for 01.13. Library hashes are in
[toolchain-reference.json](toolchain-reference.json).

See [DEVELOPMENT.md](../docs/DEVELOPMENT.md) for tests and source exports.
