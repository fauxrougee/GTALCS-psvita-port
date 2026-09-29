#pragma once
// Host-test CRC adapter only. Vita builds use VitaSDK zlib; check-movie-arm.py
// separately exercises that linked ARM implementation. Avoid requiring a
// second native Windows zlib SDK just to exercise the file/queue parser.
#include <cstdint>
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
inline uLong crc32(uLong initial, const Bytef *bytes, uInt length)
{
    struct Table {
        uint32_t value[256];
        Table() {
            for(unsigned i=0; i<256; ++i) {
                uint32_t c=i;
                for(int k=0; k<8; ++k) c=(c>>1)^((c&1)?0xedb88320U:0);
                value[i]=c;
            }
        }
    };
    static const Table table;
    if(!bytes) return 0;
    uint32_t crc=uint32_t(initial)^0xffffffffU;
    for(uInt i=0;i<length;++i) crc=(crc>>8)^table.value[(crc^bytes[i])&255];
    return crc^0xffffffffU;
}
