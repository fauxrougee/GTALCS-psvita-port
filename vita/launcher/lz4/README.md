LZ4 1.10.0, unmodified `lib/lz4.c`, `lib/lz4.h`, `lib/LICENSE` from:
https://github.com/lz4/lz4/tree/v1.10.0/lib

BSD 2-Clause license, reproduced in LICENSE and source headers.
Only the bounded LZ4_decompress_safe API is used at runtime. The movie packer
uses liblz4's HC compressor offline; playback does not encode anything.
