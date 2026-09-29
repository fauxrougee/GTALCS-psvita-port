# Testing, source exports, and publishing

## Pre-release checks

```sh
python tools/vita/check-perf-hooks.py
python tools/vita/check-perf-stats.py
python tools/vita/check-render-cache.py
python tools/vita/check-async-log.py
python tools/vita/build.py
python tools/vita/check-software-movie.py
python tools/vita/check-software-playback.py
```

Host tests use GCC with ASan/UBSan on Linux, or MSVC with ASan on Windows.
On Windows, install the x64 C++ tools and AddressSanitizer (locally validated
with MSVC 14.44). `VITA_TEST_VCVARS` and `VITA_TEST_MSVC_VERSION` allow you
to select a different installation. These tools do not build the ARM game.

The player tests check video frames, audio, malformed files, the video queue,
and the transition to the game. Vita system calls are mocked; these tests
do not replace testing on a console. The full playback test takes about
111 seconds, matching the video duration.

To test the ARM decoder linked into the release:

```sh
python -m pip install unicorn
python tools/vita/check-movie-arm.py --limit 16
# Omit --limit to check every frame; this takes longer.
```

Measure FPS on the Vita; host tests do not predict device performance.
The gameplay log is overwritten on each launch. Keep `intro.log` when
investigating player issues, along with the exact ELF files for the installed VPK.

## Exporting sources

```sh
python tools/vita/export-source.py
```

The archive in `dist/` contains the current sources, notices, dependencies
at their recorded revisions, and the edited video. The full Vita patch is
included and applied to librw by CMake. SDK files, old VPKs, crash dumps,
logs, save files, gameplay data, and personal paths are excluded.
The archive can be extracted into a new directory and built with an installed
SDK, without access to the original workspace.

When publishing an upstream Git checkout, retain its submodules and the patch.
This repository and exported source archives already include the dependencies.
No `.git` metadata is included in the ZIP. Do not add `assets/`, `sdk/`,
`build/`, `dist/`, or `.local/` to the source repository.

To publish an extracted archive as a new Git repository, create an empty
repository on GitHub, then run the following in the extracted directory.
Replace the example URL with your repository's URL:

```sh
git init -b main
git add .
git commit -m "Prepare reLCS PS Vita 01.15 with full intro"
git remote add origin https://github.com/YOUR-ACCOUNT/YOUR-REPOSITORY.git
git push -u origin main
```

The edited MP4 is approximately 11 MB and is tracked directly by Git, without
LFS. Only the MP4 is versioned, not the large generated VTM. VPKs and symbols
belong in **Releases**, not in source history. The workflows produce build
artifacts without automatically publishing releases or tags.
