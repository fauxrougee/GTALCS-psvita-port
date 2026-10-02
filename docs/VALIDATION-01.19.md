# 01.19: LiveArea updater

Status: locally compiled and checked; **not yet published or validated by
installing an update on a physical Vita**. The latest public release remains
01.18.

## Change

The LiveArea Update tile launches the internal updater SELF within `RELCS0001` instead
of starting the intro or engine. This utility checks the latest public
release's `update.txt`, downloads the full VPK over verified HTTPS, checks
its size/SHA-256, extracts it into a separate staging directory, validates
the ZIP and SFO, and calls the system promoter to update `RELCS0001`.
It leaves `ux0:data/reLCS/` intact, apart from its own `update.log`.

No separate application is installed: the VPK contains no updater SFO,
LiveArea or icon. Close the game before opening UPDATE. Normal startup makes
one silent background metadata check; a newer release is announced by a
native dialog at the main menu, or the next pause menu if loading finishes
first. Installation still opens only through UPDATE. After verification and extraction, networking is
closed and `app0:` is unmounted before calling the promoter. Failure to
unmount blocks installation. The updater exits after installation so START
can mount the updated game again.

## Local checks

- Startup notification control flow tested with native ASan and Vita/thread
  stubs: asynchronous start, French/English messages, newer/equal/older versions,
  offline and HTTP failures, invalid metadata/SFO, thread failures, dialog
  failure, one notification per launch and network cleanup before rendering.
- Production startup transfer tested for the offline skip, six-second request
  timeout, 4 KiB limit, TLS/HTTP errors, no input polling or framebuffer drawing
  on the worker, and release of the network heap. Physical input tests check
  held confirmation suppression, release and the following fresh press.

- Vita cheat entry now uses physical button presses, independent of gameplay
  bindings and controller/keyboard switching. The legacy GTA III pad table is
  replaced on Vita by 32 LCS PSP/PS2 combinations for existing effect handlers.
  Native ASan tests exercise all 32 dispatches, held/repeated buttons, L/R
  alias deduplication, and incomplete, incorrect and reversed sequences.
  These tests record handler calls; they do not execute gameplay effects.

- Full native Windows VitaSDK build, including the unchanged movie/audio.
- Game, launcher and updater ARM/Sony ELF load segments checked for overlap.
- Complete VPK checked against its executable/resource inputs, ZIP CRCs,
  game title ID, APP_VER, extended-memory flag and absence of a helper app.
- Production parser/extractor run with MSVC x64 AddressSanitizer: streamed
  stored/deflated archives, exact extraction of every file in the complete
  VPK, cancellation, folder failures, invalid paths, duplicate names,
  symlinks, bad sizes, header disagreements, corruption, missing game files,
  and wrong SFO title/version/memory attributes.
- Production transfer code tested with controlled HTTP and Vita I/O calls:
  bounded writes, stale partial replacement, resume, one retry when ranges
  fail, cancellation, truncated bodies, HTTP 404, TLS errors, SHA-256 failure
  before installation, and verification cancellation without discarding a
  reusable download.
- Real HTTPS request through GitHub redirects to the 01.18 checksum asset,
  with the bundled CA roots and certificate/hostname checks enabled. An
  unavailable CA bundle was correctly rejected. This uses host libcurl;
  it does not prove the Vita's TLS handshake works.
- Production launcher tested with controlled Vita calls: Update success and
  load-exec failure never initialize video/audio/the game. Normal intro skip,
  held-button skip and startup-error handoff remain functional.
- Production updater launch/install control flow tested with Vita calls
  replaced by stubs: internal SELF path, framebuffer closed before load-exec,
  no helper promotion, networking closed before unmount, no installation
  after an unmount error, and reporting of a promoter failure.

## Console checks still required

1. Install the complete test VPK over 01.18. Confirm START and the full intro
   still work, and UPDATE opens the small utility without playing the intro.
2. Confirm no new home-screen bubble, updater UI, Wi-Fi/TLS, installed
   version detection, and the no-compatible-release/up-to-date messages.
3. Test a newer release prepared with a matching `update.txt`: cancel and
   resume the download, then confirm app0 unmount and promotion succeed while
   running under the game title. Close the page and START the new version. Check saves,
   settings and game data after restarting the Vita.
4. Exercise insufficient space, offline operation and failed installation.
   Preserve `ux0:data/reLCS/update.log` for failures.
5. During gameplay, enter health, money, all three weapon sets and Rhino codes
   from [the cheat guide](CHEATS.md). Repeat on foot and in a vehicle, including
   custom gameplay bindings. Confirm HUD feedback, gameplay effects and stable
   weapon/vehicle streaming. Check the remaining mapped effects separately.
6. With a newer compatible release available, check the native notification
   after loading, its language and confirmation input. Repeat offline and
   with an equal/older version; there should be no dialog or added loading
   wait. If START is selected before the check finishes, open the pause menu.

Publishing the new VPK alone does not activate in-console updates. The
matching `update.txt` must be an asset of the same tested public **latest**
release. No public metadata or release was changed during these local checks.
