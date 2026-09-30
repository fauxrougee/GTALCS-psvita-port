# 01.16 test notes

This update reduces repeated renderer state changes, adds opaque world shader
variants and groups compatible particles without changing their order.
Resolution, textures, draw distance and effect counts retain their existing settings.

- Native Windows game, full-intro and benchmark builds: passed.
- A clean source export also compiles and packages through the native VitaSDK.
- Package metadata, extended memory attribute, ARM ELF layout and ZIP CRCs: passed.
- Full intro decoder: all 2,774 frames and complete PCM audio match the independent
  decode; seeking, concurrent frame queues and damaged files pass host checks.
- Uniform cache: 15,000 mock draws, program switches/recreation and bit-pattern checks.
- Reflections: 12,000 comparisons against the uncached calculation.
- Vertex input: 30,000 layouts, missing attributes, buffer storage reuse and resets.
- Indexed draws: 4,000 meshes, unchanged indices/offsets and fallback paths.
- Particles: 181,092 draw requests across 200 synthetic scenes, with matching
  arguments, order, texture/blend/depth state, random-number use and trail updates.
- Frame accounting, statistics and benchmark/report regressions: passed.
- The librw patch applies to unmodified dependency sources. Standalone folders,
  Git checkouts and archives inside another checkout use the correct destination;
  a second configuration keeps the patched files unchanged.

The user accepted the build for release. No new measured console benchmark was
provided after the optimization changes; sustained 45–60 FPS is not established.
Host rendering checks use mocked GL calls and cannot validate actual GPU output.

The previous console benchmark included long streaming pauses. These renderer
changes do not address blocking asset loads. See the
[renderer notes](VITA-PERFORMANCE.md) and [benchmark guide](BENCHMARK.md).
