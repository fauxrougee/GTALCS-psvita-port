#pragma once
#include <cstdint>
// Reversible colour decorrelation. RGBA -> three planes (R-G, G, B-G).
// Arithmetic is modulo 256; all original RGB bytes are restored exactly.
inline void IntroPackColorPlanes(const uint32_t *rgba,uint8_t *out,unsigned count)
{
	for(unsigned i=0;i<count;++i) {
		const uint32_t p=rgba[i]; const uint8_t g=p>>8;
		out[i]=uint8_t(p)-g; out[i+count]=g; out[i+2*count]=uint8_t(p>>16)-g;
	}
}
extern "C" void IntroExpandColorPlanes(const uint8_t *in,uint32_t *rgba,unsigned count);
