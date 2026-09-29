#ifdef PSP2
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <zlib.h>
#include "vita_loading.h"

namespace {
void *allocation=nullptr;
uint32_t *pixels=nullptr;
SceUInt64 began=0;
constexpr unsigned Width=960,Height=544,Bytes=Width*Height*4;
// A small font for the loading label; no GL calls or shader compilation.
const char *letters="CHARGEMENT.";
const uint8_t glyphs[][7]={
	{14,17,16,16,16,17,14},{17,17,17,31,17,17,17},{14,17,17,31,17,17,17},
	{30,17,17,30,20,18,17},{14,17,16,23,17,17,15},{31,16,16,30,16,16,31},
	{17,27,21,21,17,17,17},{31,16,16,30,16,16,31},{17,25,25,21,19,19,17},
	{31,4,4,4,4,4,4},{0,0,0,0,0,12,12}};
void Label()
{
	for(unsigned y=488;y<530;++y) for(unsigned x=290;x<670;++x) pixels[y*Width+x]=0xff100817;
	const char *s="CHARGEMENT...";
	for(unsigned i=0;s[i];++i) {
		const char *p=strchr(letters,s[i]); if(!p) continue;
		for(unsigned y=0;y<7;++y) for(unsigned x=0;x<5;++x) if(glyphs[p-letters][y]&(16>>x))
			for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
				pixels[(499+y*3+dy)*Width+364+i*18+x*3+dx]=0xffeeeeee;
	}
}
}

void VitaLoadingStart()
{
	if(allocation) return;
	// Same hook/pool as the previously working CPU mini-intro. Keep the frame
	// alive until both display queues have switched to the game's own buffers.
	allocation=vglAlloc(Bytes+255,VGL_MEM_VRAM);
	if(!allocation) return;
	pixels=reinterpret_cast<uint32_t*>((reinterpret_cast<uintptr_t>(allocation)+255)&~uintptr_t(255));
	for(unsigned i=0;i<Width*Height;++i) pixels[i]=0xff100817;
	FILE *f=fopen("app0:boot/loading.rgba.z","rb");
	if(f) {
		if(fseek(f,0,SEEK_END)==0) {
			long size=ftell(f);
			if(size>0 && size<2*1024*1024 && fseek(f,0,SEEK_SET)==0) {
				auto *packed=static_cast<unsigned char*>(malloc(size));
				auto *art=static_cast<uint32_t*>(malloc(840*500*4));
				uLongf bytes=840*500*4;
				if(packed && art && fread(packed,1,size,f)==static_cast<size_t>(size) &&
				   uncompress(reinterpret_cast<Bytef*>(art),&bytes,packed,size)==Z_OK && bytes==840*500*4)
					for(unsigned y=0;y<500;++y) memcpy(pixels+(y+22)*Width+60,art+y*840,840*4);
				free(packed); free(art);
			}
		}
		fclose(f);
	}
	Label();
	SceDisplayFrameBuf fb={}; fb.size=sizeof(fb); fb.base=pixels;
	fb.pitch=Width; fb.width=Width; fb.height=Height; fb.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	const int result=sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);
	if(result<0) { vglFree(allocation); allocation=nullptr; pixels=nullptr; return; }
	sceDisplayWaitVblankStartMulti(1);
	began=sceKernelGetProcessTimeWide();
	printf("[VITA] Loading artwork visible; shaders and menu are being prepared\n");
}

void VitaLoadingRelease()
{
	if(!allocation) return;
	for(unsigned sync=SCE_DISPLAY_SETBUF_IMMEDIATE;sync<=SCE_DISPLAY_SETBUF_NEXTFRAME;++sync) {
		SceDisplayFrameBuf fb={}; fb.size=sizeof(fb);
		if(sceDisplayGetFrameBuf(&fb,static_cast<SceDisplaySetBufSync>(sync))<0 || fb.base==pixels) return;
	}
	vglFree(allocation); allocation=nullptr; pixels=nullptr;
	printf("[VITA] First game display after %.2f seconds; loading artwork released\n",
	       (sceKernelGetProcessTimeWide()-began)/1000000.0);
}
#endif
