// Movie-only process. The verified game SELF is started after playback via
// load-exec, so no decoder, display worker or modified renderer enters the game.
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include "vita_boot_movie.h"
#include "intro_log.h"

extern "C" {
int _newlib_heap_size_user = 64*1024*1024;
unsigned int sceUserMainThreadStackSize = 256*1024;
}

namespace {
constexpr unsigned Width=960, Height=544, BufferStride=2*1024*1024;
SceUID displayBlock=-1;
uint32_t *buffers[2];

bool AllocateDisplay()
{
	displayBlock=sceKernelAllocMemBlock("intro display", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
	                                    2*BufferStride, nullptr);
	IntroLog("[INTRO] Display allocation: 0x%08X\n",displayBlock);
	if(displayBlock<0) return false;
	void *base=nullptr;
	const int result=sceKernelGetMemBlockBase(displayBlock,&base);
	IntroLog("[INTRO] Display base: result=0x%08X address=%p\n",result,base);
	if(result<0 || !base) return false;
	buffers[0]=static_cast<uint32_t*>(base);
	buffers[1]=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(base)+BufferStride);
	for(auto pixels: buffers) std::fill(pixels,pixels+Width*Height,0xff000000U);
	return true;
}

bool Present(unsigned back)
{
	SceDisplayFrameBuf fb={};
	fb.size=sizeof(fb); fb.base=buffers[back]; fb.pitch=Width;
	fb.width=Width; fb.height=Height; fb.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	const int result=sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);
	if(result<0){ IntroLog("[INTRO] Present failed: 0x%08X\n",result); return false; }
	const int wait=sceDisplayWaitVblankStartMulti(1);
	if(wait<0) IntroLog("[INTRO] Vblank failed: 0x%08X\n",wait);
	return wait>=0;
}

void ReleaseDisplay()
{
	if(displayBlock<0) return;
	// Wait for both scanout queues to stop referencing our pixels before freeing.
	if(sceDisplaySetFrameBuf(nullptr,SCE_DISPLAY_SETBUF_NEXTFRAME)<0) return;
	if(sceDisplayWaitVblankStartMulti(2)<0) return;
	for(unsigned sync=SCE_DISPLAY_SETBUF_IMMEDIATE; sync<=SCE_DISPLAY_SETBUF_NEXTFRAME; ++sync){
		SceDisplayFrameBuf fb={}; fb.size=sizeof(fb);
		if(sceDisplayGetFrameBuf(&fb,static_cast<SceDisplaySetBufSync>(sync))<0) return;
		if(fb.base && (fb.base==buffers[0] || fb.base==buffers[1])) return;
	}
	sceKernelFreeMemBlock(displayBlock); displayBlock=-1;
}
}

int main()
{
	sceIoMkdir("ux0:data/reLCS",0777);
	IntroLogInit();
	IntroLog("[INTRO] Isolated movie launcher v8; compact lossless movie; stable engine with startup cache\n");
	SceIoStat videoStat={};
	const int statResult=sceIoGetstat("app0:boot/intro.vtm",&videoStat);
	IntroLog("[INTRO] Bundled movie: stat=0x%08X bytes=%llu\n",statResult,
	         static_cast<unsigned long long>(videoStat.st_size));
	IntroLogFlush();
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
	if(AllocateDisplay() && Present(0) && VitaBootMovieReserve() && VitaBootMovieStart()){
		IntroLog("[INTRO] Entering playback loop\n");
		unsigned back=1;
		for(;;){
			sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
			const int result=VitaBootMoviePoll(buffers[back]);
			if(result<0) break;
			if(result>0){ if(!Present(back)) break; back^=1; }
			else sceDisplayWaitVblankStartMulti(1);
		}
	}else IntroLog("[INTRO] Startup failed before playback loop\n");
	VitaBootMovieClose();
	VitaBootMovieRelease();
	ReleaseDisplay();
	IntroLog("[INTRO] Loading stable engine with startup cache app0:game.bin\n");
	IntroLogFlush();
	const int result=sceAppMgrLoadExec("app0:game.bin",nullptr,nullptr);
	IntroLog("[INTRO] LoadExec returned 0x%08X\n",result);
	IntroLogFlush();
	sceKernelExitProcess(result);
	return result;
}
