# 01.15 test notes

This update removes the FPS/CPU/RAM overlay. Diagnostic logging stays enabled.

- Native Windows build and VPK packaging: passed.
- Overlay drawing function: absent from the linked game.
- Performance sampling functions: still present.
- Intro player, video/audio, loading image, icon, and LiveArea: SHA-256 matches with 01.14.
- Package version, extended memory attribute, ZIP contents, and ELF segments: passed.

The intro files are unchanged, so the [01.14 playback checks](VALIDATION-01.14.md) still apply. No new physical-console test was performed for this update.
