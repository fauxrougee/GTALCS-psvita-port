# Version 01.14 validation

Local validation performed on September 29, 2026, using native Windows tools.

- The engine retains the behavior of version 01.13, which was tested on a
  console. Preparing this release changes the build, distribution, and
  documentation; only comment references change in the engine sources.
- The game and intro player are built from source. A second full build
  succeeds from the exported archive in a fresh directory containing spaces,
  without a Git repository, old VPK, or old executable.
- The `--no-intro` option builds a direct-launch package; switching back to
  the full intro build is also verified.
- The container regenerated from the included MP4 is byte-for-byte identical
  to the previously working full intro: 2,774 frames and 5,326,080 stereo
  samples, approximately 111 seconds.
- The host reader checks every frame and all PCM audio, video seeking, the
  concurrent queue, and rejection of malformed files.
- The ARM functions actually linked into the player run under Unicorn for
  all 2,774 frames: CRC, LZ4 decompression, colors, and buffer bounds pass.
- Full playback simulation and skip, audio/video failure, synchronization,
  and missing-video scenarios pass. Vita system calls are mocked; any frames
  that become too old according to the host clock are counted separately.
- Performance accounting, rendering value preservation, and asynchronous
  logging tests pass with AddressSanitizer.
- The VPK passes checks for ZIP CRCs, executable and media hashes, metadata,
  extended memory, icon, and LiveArea. ARM ELF load segments do not overlap
  in either the game or the intro player.

These checks do not measure FPS on a console. The 01.14 VPK still needed
testing on a physical PS Vita at the time of this validation. The supplied
GitHub Actions workflow was not run on GitHub during these local checks.
