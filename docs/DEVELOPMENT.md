# Tests and release tools

Set up the [build environment](../vita/README.md) first. The commands below use `python`; substitute the interpreter from your virtual environment.

## Engine checks

```sh
python tools/vita/check-perf-hooks.py
python tools/vita/check-perf-stats.py
python tools/vita/check-render-cache.py
python tools/vita/check-render-state.py
python tools/vita/check-librw-patch.py
python tools/vita/check-async-log.py
python tools/vita/check-save-paths.py
python tools/vita/check-vita-controls.py
```

These check frame accounting, rendering values, the asynchronous logger, save paths/error handling, and controller bindings. Host tests need GCC with ASan/UBSan on Linux, or MSVC x64 with AddressSanitizer on Windows. MSVC 14.44 was used for the local checks. Set `VITA_TEST_VCVARS` or `VITA_TEST_MSVC_VERSION` to select another installation.

To check benchmark calculations and the report tool:

```sh
python tools/vita/check-bench-metrics.py
python tools/vita/check-bench-report.py
```

Build the separate console app with `python tools/vita/build.py --benchmark`.
See [BENCHMARK.md](BENCHMARK.md) for running it and comparing results.

## Intro checks

Build with the intro first, then run:

```sh
python tools/vita/build.py
python tools/vita/check-software-movie.py
python tools/vita/check-software-playback.py
```

The reader test checks the frames, PCM audio, seeking, and malformed files. The playback test exercises the player with mocked Vita calls, including skipping and handing off to the game. The full playback run takes about 111 seconds.

To test the ARM decoder linked into the release:

```sh
python -m pip install unicorn
python tools/vita/check-movie-arm.py --limit 16
```

Leave off `--limit` to check all 2,774 frames. These tests do not measure performance on a Vita; use a console for FPS comparisons.

## Package and export

`build.py` checks the VPK contents and ELF layout before copying outputs to `dist/`. Keep the VPK, ELF files, and checksums together so crash reports can be matched to the right build.

To export a source ZIP:

```sh
python tools/vita/export-source.py
```

The ZIP includes the dependencies and edited MP4. Game data, the SDK, logs, dumps, and build outputs stay out of the archive. Extract it into a clean directory when checking that a release builds from source.

Upload VPKs to GitHub Releases. Keep `assets/`, `sdk/`, `build/`, `dist/`, and `.local/` out of commits. The workflows upload artifacts; they do not publish releases automatically.

## Recorded checks

- [01.16: renderer optimizations and benchmark](VALIDATION-01.16.md)
- [01.17: Vita save folder and error reporting](VALIDATION-01.17.md)
- [01.18: D-pad left and existing controller settings](VALIDATION-01.18.md)
- [01.15: removal of the performance overlay](VALIDATION-01.15.md)
- [01.14: full intro and source build](VALIDATION-01.14.md)
