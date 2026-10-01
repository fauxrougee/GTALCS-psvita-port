# reLCS Benchmark

The benchmark is a separate Vita app that runs scripted scenes using the same
engine as the game. Use it to compare builds and identify expensive parts of
the frame. It does not predict performance from a PC test.

## Install and run

1. Set up the converted game data and shader compiler using the
   [installation guide](../README.md#install).
2. [Build from source](../vita/README.md) with `--benchmark`, then install
   `dist/reLCS-01.17-benchmark.vpk`. The public release contains only the complete game.
3. Launch **reLCS Benchmark** and let the complete run finish.
4. Copy the new dated folder from `ux0:data/reLCS/benchmark/` to your PC.

The benchmark has its own title ID, `RLCSBENCH`, and bubble. It uses the existing
data in `ux0:data/reLCS/`, skips the intro and suppresses settings/save writes.
It does not replace the game's `log.txt`.

The full run covers flythroughs, crowd and traffic density, AI driving,
explosions, night rain, island streaming, teleports and static isolation tests.
Benchmark screens and generated reports currently use French labels.

## Result files

| File | Contents |
| --- | --- |
| `results.json` | Build, console settings, per-test measurements and capability flags. |
| `frames.csv` | Per-frame timings and counters. |
| `summary.txt` | Text summary of the run. |
| `log.txt` | Startup and diagnostic output. |

Keep these files together. Partial runs are supported, but a complete run with
the same console settings gives a better before/after comparison.

## Read or compare results

The report tool needs Python 3.10+ and uses the standard library:

```sh
python tools/vita/bench-report.py NEW_RESULTS
python tools/vita/bench-report.py NEW_RESULTS --text
python tools/vita/bench-report.py --compare OLD_RESULTS NEW_RESULTS -o comparison.html
```

Replace `OLD_RESULTS` and `NEW_RESULTS` with the dated result folders. The HTML
report is self-contained and can be opened in a browser without a server.

Check average FPS, 1% lows and long frame times together. A higher average can
still hide loading pauses. Avoid changing clocks, game data or console plugins
between runs unless that change is what you are testing.

## What the timings mean

- CPU work covers the main thread up to buffer swap; swap includes waiting.
- GPU timing uses a polled completion fence. It is approximate and may miss GPU
  work that starts before the swap marker. Missing measurements stay unavailable.
- CPU and GPU work overlap. Adding their times does not give frame time.
- GL counters describe submission calls and uploaded data, not GPU execution time.
- Disk counters and streaming tests help identify blocking loads.

Isolation tests deliberately disable parts of the scene or replace draw work.
These are diagnostic comparisons, not the game's rendering settings. In
particular, removing particles from a sunny, quiet view does not measure the
cost of dense smoke or rain.

## Build the benchmark

```powershell
.\tools\vita\build-windows.ps1 --benchmark --jobs 4
```

Or use `python tools/vita/build.py --benchmark --jobs 4` on either supported
platform. The build directory is `build/vita-bench/`. Outputs in `dist/` include
the VPK, matching game ELF, package metadata and checksums. FFmpeg and movie
conversion are not needed for this build.

Host regression checks:

```sh
python tools/vita/check-bench-metrics.py
python tools/vita/check-bench-report.py
```

For the renderer changes in this release, see [VITA-PERFORMANCE.md](VITA-PERFORMANCE.md).
