# 01.17 save fix

[Issue #1](https://github.com/fauxrougee/GTALCS-psvita-port/issues/1) reported that
the game displayed a successful save but left the load menu empty. Creating
`ux0:data/reLCS/userfiles/` manually worked around the missing folder.

The Vita folder helper now uses an absolute `sceIoMkdir` path rather than
resolving a directory that does not exist. Visiting the settings/user-files path
keeps the working directory at `ux0:data/reLCS/`, so relative game-data and save
paths remain valid. Save names use `/` on Vita; existing filenames and serialized
save data are unchanged.

Failed file creation returns a nonzero result to the menu. Failed writes and
buffered flush/close failures also return an error. The file is closed on every
save attempt, and the stdio error accessor checks `ferror` as well as EOF.

Run the regression check with:

```sh
python tools/vita/check-save-paths.py
```

The host test executes the production folder, save, load-menu discovery and
error-handling functions with real files and controlled Vita I/O. It covers a
fresh installation, an existing folder, eight slots, repeated menu visits,
access to `models/gta3.img`, failed folder creation, a file blocking the folder,
failed opens/writes/checksum writes/close, recovery after failure and file-handle
cleanup. It runs under MSVC AddressSanitizer on Windows or GCC sanitizers on Linux.

The regression check passed with native MSVC/ASan. The complete game and intro
launcher also built with the native VitaSDK; package contents, ZIP CRCs,
version/title ID, extended memory attribute and ARM ELF checks passed.

The game-state payload and Vita device are mocked. Host tests do not confirm
that a real console can resume a particular saved game. Install the full VPK
over the previous version, save, restart the app and load the slot to verify
that final step. Files that were never written by the previous version cannot
be recovered.
