# GTA LCS Update

The complete build includes an **UPDATE** tile in the game's LiveArea. Close
the game before opening it. The launcher uses load-exec to open the bundled
`app0:updater/eboot.bin` within `RELCS0001`. It does not install or register a
second application. The updater has no separate SFO, icon or LiveArea.

The utility stages and verifies the release, closes networking, releases
`app0:` with `sceAppMgrUmount`, then asks the system promoter to replace
`RELCS0001`. A failed unmount stops installation. Once installed, close the
game's LiveArea page and select **START**, allowing a fresh application mount.
This installation path still needs a complete physical-console test; native
host tests verify its ordering and failure handling, not the system promoter.
The updater does not delete the installed game or write its saves/settings.
Its working files stay in `ux0:data/reLCS-update/` and diagnostics append to
`ux0:data/reLCS/update.log`.

The earlier local test build registered `RLCUPD001`. If its **GTA LCS Update**
bubble is already installed, delete that old utility from the home screen.
The new build does not create it again. Do not delete the game bubble.

## Download and install

Updates use the public GitHub release asset `update.txt`. The updater compares
the installed game's SFO version against that manifest. Older and equal
versions are not installed. The download URL must identify the complete VPK
in this repository at the exact announced version.

libcurl with Mbed TLS verifies HTTPS certificates and hostnames using the
bundled CA roots. The Vita's clock must be correct. A download can resume
only when its stored manifest is identical to the latest metadata. The final
size and SHA-256 must match before extraction begins. Interrupted downloads
are retained; failed checksums discard the file.

Extraction is bounded by the announced uncompressed size. ZIP entries with
unsafe paths, duplicates, encryption, symbolic links, overlapping ranges,
unsupported compression, or disagreeing headers are rejected. Each file's
length and CRC are checked. Before promotion, the SFO must identify
`RELCS0001`, the announced version, and the extended-memory attribute.
Cancellation is available during download, verification, and extraction.
Installation itself cannot be canceled.

The free-space check reserves the VPK, two copies of the uncompressed
package, and 16 MiB for installation overhead. A full update currently needs
about 542 MiB free. Updates download the complete VPK, including the intro.
Delta updates are not implemented.

## Publishing an update

Build and test the complete VPK, then run:

```sh
python tools/vita/prepare-update-release.py dist/reLCS-01.19-intro-complete.vpk
```

Upload these three files together to the `v01.19` release:

- `reLCS-01.19-intro-complete.vpk`
- `SHA256SUMS.txt`
- `update.txt`

Use the new version in filenames and the release tag on subsequent updates.
Mark a tested public release as **latest**. GitHub's
`releases/latest/download/update.txt` link then points players to that version.
Drafts and prereleases do not change this channel. The preparation script
verifies package identity, the full intro and updater, filename, ZIP CRCs,
sizes, and SHA-256. It never publishes a release itself.

An existing 01.18 installation needs one manual VPK installation to acquire
this feature. If the latest public release lacks `update.txt`, the utility
reports that no compatible release is available. It does not install an
older release as a fallback.

## Third-party material

- libcurl: curl license, `licenses/curl.txt`.
- Mbed TLS: Apache 2.0, `licenses/mbedtls.txt`.
- Zstandard: BSD license, `licenses/zstd.txt`.
- Debug font from the VitaSDK samples/PSPSDK: BSD license,
  `licenses/font.txt`; original notices retained in `font.h`.
- Homebrew package-header template from
  [VitaShell](https://github.com/TheOfficialFloW/VitaShell/blob/master/resources/head.bin),
  and header-generation algorithm from `package_installer.c`: GPL 3.0 or
  later, `licenses/vitashell.txt`. `tools/vita/make-update-head.py` carries
  this license separately from the port.
- CA bundle from [curl's Mozilla certificate export](https://curl.se/docs/caextract.html).

Target libraries are supplied by VitaSDK's `curl-mbedtls`, `mbedtls`, and
`zstd` packages. Their sources and Vita patches are maintained in
[vitasdk/packages](https://github.com/vitasdk/packages).
