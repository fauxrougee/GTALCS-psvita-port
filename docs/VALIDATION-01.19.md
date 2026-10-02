# 01.19: LiveArea updater

Status: locally compiled and checked; **not yet published or validated by
installing an update on a physical Vita**. The latest public release remains
01.18.

## Change

The LiveArea Update tile launches an independent `RLCUPD001` utility instead
of starting the intro or engine. This utility checks the latest public
release's `update.txt`, downloads the full VPK over verified HTTPS, checks
its size/SHA-256, extracts it into a separate staging directory, validates
the ZIP and SFO, and calls the system promoter to update `RELCS0001`.
It leaves `ux0:data/reLCS/` intact, apart from its own `update.log`.

The helper is installed automatically from the bundled files and adds a
small GTA LCS Update bubble. It uses no Shell plugin. Close the game before
opening the Update tile. Networking is isolated from normal game startup.

## Local checks

- Full native Windows VitaSDK build, including the unchanged movie/audio.
- Game, launcher and updater ARM/Sony ELF load segments checked for overlap.
- Complete VPK checked against its executable/resource inputs, ZIP CRCs,
  title IDs, APP_VER and extended-memory flag.
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
  helper failure never initialize video/audio/the game. Normal intro skip,
  held-button skip and startup-error handoff remain functional.

## Console checks still required

1. Install the complete test VPK over 01.18. Confirm START and the full intro
   still work, and UPDATE opens the small utility without playing the intro.
2. Confirm helper promotion, bubble creation, its UI, Wi-Fi/TLS, installed
   version detection, and the no-compatible-release/up-to-date messages.
3. Test a newer release prepared with a matching `update.txt`: cancel and
   resume the download, then install and start the new version. Check saves,
   settings and game data after restarting the Vita.
4. Exercise insufficient space, offline operation and failed installation.
   Preserve `ux0:data/reLCS/update.log` for failures.

Publishing the new VPK alone does not activate in-console updates. The
matching `update.txt` must be an asset of the same tested public **latest**
release. No public metadata or release was changed during these local checks.
