# 01.14 test notes

Recorded September 29, 2026, using native Windows tools. The release restored the full intro while keeping the engine behavior from 01.13.

| Check | Result |
| --- | --- |
| Build from the source ZIP in a fresh directory, including a path with spaces | Passed |
| Build without the intro, then switch back to the full build | Passed |
| Regenerate the video container from the included MP4 | Byte-for-byte match with the working intro |
| Decode 2,774 frames and 5,326,080 stereo audio samples | Passed |
| Seeking, concurrent queue, and rejection of malformed files | Passed |
| Linked ARM CRC, LZ4, and color conversion code under Unicorn, all frames | Passed |
| Full playback, skipping, audio/video failures, and missing-video handling | Passed with mocked Vita calls |
| Frame accounting, rendering values, and asynchronous logging | Passed with AddressSanitizer |
| VPK contents, metadata, memory attribute, artwork, ZIP CRCs, and ELF load segments | Passed |

Playback tests count frames dropped because of host timing separately. They do not measure Vita FPS.

The 01.14 VPK had not been tested on a physical console at the time of these checks. GitHub Actions was not part of this local validation.
