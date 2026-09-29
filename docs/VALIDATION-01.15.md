# Version 01.15 validation

Version 01.15 removes the FPS/CPU/RAM overlay. Its drawing function, call,
and declaration are removed. Measurements used by diagnostic reports
remain active.

Local checks using native Windows tools:

- The game builds and the VPK with the full intro is created successfully.
- The overlay drawing function is absent from the compiled game's symbols;
  measurement collection functions are still present.
- The intro player, video with audio, loading screen, icon, and LiveArea
  are identical to 01.14, verified by SHA-256 hashes of the packaged files.
- Build tools verify the 01.15 metadata, extended memory, VPK contents,
  ZIP CRCs, and ELF segments.

The [full intro checks performed for 01.14](VALIDATION-01.14.md) still apply
to these unchanged files. No new test on a physical PS Vita was performed
while making this change.
