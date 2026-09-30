# Vita renderer changes — 01.16

This build targets the rendering costs in the Vita benchmark from September 30,
2026. Performance after these changes has **not been measured on a Vita yet**.
45–60 FPS is the target, not a measured result or a guarantee.

## What the baseline showed

The baseline ran at 960 × 544, with CPU/bus/GPU/xbar clocks of
444/222/222/166 MHz.

| Scene | Average FPS | GPU time per frame |
| --- | ---: | ---: |
| Portland | 35.76 | 26.39 ms |
| Night rain | 25.02 | 39.22 ms |
| Mayhem | 24.76 | 40.12 ms |

The static Chinatown and Bedford views submitted about 1,670 and 1,959 draw calls
per frame. Tests that removed the driver calls exposed substantial CPU submission
cost. Rain and mayhem also took more GPU time. A sunny static view with few
particles cannot establish the cost of dense smoke or rain.

The island run included a 3.2-second streaming hitch. Rendering changes do not
fix blocking asset loads. CPU and GPU timings overlap; do not add them together.

## Changes in this build

- Custom scalar, three-component and array uniforms now use the existing
  per-program cache. Values are compared by their bits. Changing or recreating a
  shader still uploads its current values.
- Vertex arrays stay enabled between compatible layouts. Attributes missing from
  the next layout are disabled before drawing. Vertex pointers are always
  refreshed, including when a VBO keeps its name but changes storage.
- Indexed meshes supply the bounds already calculated during instancing through
  `glDrawRangeElements`. Empty meshes, unavailable entry points and invalid bounds
  retain the previous draw path. Benchmark draw filters and counters cover both
  paths. See vitaGL's [indexed draw implementation](https://github.com/Rinnegatamante/vitaGL/blob/master/source/custom_shaders.c#L1472).
- World shaders have a variant without alpha-test discard. It is selected only
  when the engine disables alpha testing. Both world pipelines keep the original
  variant for transparency, and shader compilation failure uses the original
  shader.
- Particle batches can hold 256 sprites instead of 64. Adjacent compatible
  systems share a batch; texture, blend and 2D/3D transitions flush it. Clipped
  particles no longer force driver texture changes. Particle order, random-number
  consumption and trail updates are retained.

Resolution, textures, filtering, draw distance, traffic density and particle
counts retain their current settings. The existing clocks and absence of a
software frame limiter are also retained.

## Validation

The native host checks use production renderer functions with mocked GL calls
and AddressSanitizer:

```powershell
python tools/vita/check-render-cache.py
python tools/vita/check-render-state.py
python tools/vita/check-perf-hooks.py
python tools/vita/check-perf-stats.py
python tools/vita/check-bench-metrics.py
python tools/vita/check-bench-report.py
```

The rendering checks compare 15,000 mock draws, 12,000 reflection matrices,
30,000 vertex layouts, 4,000 indexed meshes and 181,092 particle draw requests
across 200 scenes. They cover program recreation, missing uniforms, storage
reuse, texture/blend/depth transitions, clipping and animated particles.

These checks establish CPU-side submission equivalence. They cannot validate
Vita GPU output, gameplay stability or frame rates. The librw export patch is
also checked against an unmodified submodule checkout. VPK validation checks
ARM executables, metadata, assets and archive CRCs.

## Compare on the console

Build the game and the matching benchmark with the native Windows toolchain:

```powershell
.\tools\vita\build-windows.ps1 --no-intro --jobs 4
.\tools\vita\build-windows.ps1 --benchmark --jobs 4
```

The game test VPK starts directly and uses `RELCS0001`. The benchmark uses its
own bubble, `RLCSBENCH`. Both read the existing data in `ux0:data/reLCS/`.

Run the same complete benchmark with the same console settings. Copy the new
dated results folder from `ux0:data/reLCS/benchmark/` to the PC, then compare:

```powershell
python tools/vita/bench-report.py --compare OLD_RESULTS NEW_RESULTS -o comparison.html
```

Check average FPS, 1% lows, GPU time, submission cost and streaming hitches
together. Also play through rain, tire smoke, vehicle damage and mission
transitions to check rendering and gameplay. A faster average alone does not
demonstrate sustained 45–60 FPS.
