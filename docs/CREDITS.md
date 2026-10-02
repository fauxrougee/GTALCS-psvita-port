# Credits

- **fauxrouge** - PS Vita port and presentation.
- **[reStories / reLCS](https://github.com/knackers4/res)** - the engine this port is based on, and the PS2 asset converter.
- **[aap / librw](https://github.com/aap/librw)** - RenderWare-compatible rendering.
- **[Rinnegatamante / vitaGL](https://github.com/Rinnegatamante/vitaGL)** - OpenGL support on Vita.
- **[VitaSDK](https://vitasdk.org/)** and the Vita homebrew community - toolchain and system libraries.
- **OpenAL Soft, mpg123, vitaShaRK, mathneon, and Xiph.org** - audio, shaders, and supporting libraries.
- **[LZ4](https://github.com/lz4/lz4)** - intro decompression.
- **libcurl, Mbed TLS, and Zstandard** - updater downloads, HTTPS, and verification.
- **PSPSDK/VitaSDK samples** - updater bitmap font.
- **[TheFloW / VitaShell](https://github.com/TheOfficialFloW/VitaShell)** - homebrew package-header template and generation algorithm, and reference for the independent installer process.

## Licenses and original notices

The upstream README is kept in [UPSTREAM.md](UPSTREAM.md). Dependency licenses remain with their sources in `vendor/` and `vita/launcher/lz4/`. The LZ4 license is also included in the VPK.

Updater notices are in [vita/updater/README.md](../vita/updater/README.md), with
the license texts in `vita/updater/licenses/` and the complete VPK. The
package-header generation tool is GPL 3.0 or later, as noted in its source.

This port does not replace the licenses of its dependencies or upstream code. GTA: Liberty City Stories, its characters, music, and video belong to their respective rights holders. The project is not affiliated with Rockstar Games or Take-Two Interactive.
