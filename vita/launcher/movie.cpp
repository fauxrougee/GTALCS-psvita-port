#ifdef PSP2
#include <atomic>
#include <algorithm>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include "vita_boot_movie.h"
#include "movie_file.h"
#include "movie_queue.h"
#include "intro_log.h"

namespace {
IntroMovieFile movie;
IntroFrameQueue frames;
SceUID videoThread=-1,audioThread=-1;
int audioPort=-1;
bool retained=false;
std::atomic<bool> stop(false),failed(false),audioDone(false);
std::atomic<unsigned> clockFrame(0),decoded(0),decodeSkipped(0),maxDecodeUs(0);
unsigned oldButtons=0,shown=0,displayDropped=0,lastClock=0;
SceUInt64 lastProgress=0;

int DecodeThread(SceSize,void*)
{
	unsigned next=0;
	while(!stop.load() && next<movie.Frames()) {
		uint32_t *pixels=frames.BeginWrite();
		if(!pixels) { sceKernelDelayThread(1000); continue; }
		const unsigned current=clockFrame.load();
		if(next<current) { decodeSkipped.fetch_add(current-next); next=current; }
		const SceUInt64 start=sceKernelGetProcessTimeWide();
		if(!movie.Decode(next,pixels)) {
			IntroLog("[INTRO] Frame %u failed: %s\n",next,movie.Error());
			failed.store(true); break;
		}
		const unsigned elapsed=unsigned(sceKernelGetProcessTimeWide()-start);
		if(elapsed>maxDecodeUs.load()) maxDecodeUs.store(elapsed);
		frames.Publish(next++); decoded.fetch_add(1);
	}
	return 0;
}

int AudioThread(SceSize,void*)
{
	// One 40-ms video interval per PCM block. The audio hardware paces playback;
	// loading and decoding images cannot starve it because PCM is preloaded.
	for(unsigned n=0;n<movie.Frames() && !stop.load();++n) {
		const int result=sceAudioOutOutput(audioPort,movie.Audio()+n*IntroMovieFile::SamplesPerFrame*2);
		if(result<0) {
			IntroLog("[INTRO] Audio output failed: 0x%08X frame=%u\n",result,n);
			failed.store(true); break;
		}
		clockFrame.store(n);
		if(n==0) IntroLog("[INTRO] First PCM block submitted\n");
	}
	// Output(NULL) drains the last submitted buffer before releasing its memory.
	const int result=sceAudioOutOutput(audioPort,nullptr);
	if(result<0) { IntroLog("[INTRO] Audio drain failed: 0x%08X\n",result); failed.store(true); }
	audioDone.store(true);
	return 0;
}

bool StartThread(SceUID &thread,const char *name,SceKernelThreadEntry entry,int priority)
{
	thread=sceKernelCreateThread(name,entry,priority,64*1024,0,
	                             SCE_KERNEL_CPU_MASK_USER_ALL,nullptr);
	if(thread<0) { IntroLog("[INTRO] Create %s failed: 0x%08X\n",name,thread); return false; }
	const int result=sceKernelStartThread(thread,0,nullptr);
	if(result>=0) return true;
	IntroLog("[INTRO] Start %s failed: 0x%08X\n",name,result);
	sceKernelDeleteThread(thread); thread=-1; return false;
}

bool JoinThread(SceUID &thread)
{
	if(thread<0) return true;
	SceUInt timeout=3000000;
	const int result=sceKernelWaitThreadEnd(thread,nullptr,&timeout);
	if(result<0) { IntroLog("[INTRO] Join failed: 0x%08X; retaining resources for LoadExec\n",result); return false; }
	sceKernelDeleteThread(thread); thread=-1; return true;
}
}

bool VitaBootMovieReserve()
{
	IntroLog("[INTRO] Direct RGBA/LZ4 playback; no AVPlayer, GXM or video driver\n");
	const int clock=scePowerSetArmClockFrequency(444);
	IntroLog("[INTRO] CPU clock: 0x%08X\n",clock);
	if(!movie.Open("app0:boot/intro.vtm")) {
		IntroLog("[INTRO] Movie preparation failed: %s\n",movie.Error()); return false;
	}
	if(!frames.Allocate()) { IntroLog("[INTRO] Frame queue allocation failed\n"); return false; }
	IntroLog("[INTRO] Validated movie: %u frames, %u FPS, %u PCM bytes; 8-frame queue\n",
	         movie.Frames(),IntroMovieFile::FPS,movie.AudioBytes());
	return true;
}

bool VitaBootMovieStart()
{
	stop.store(false); failed.store(false); audioDone.store(false);
	clockFrame.store(0); decoded.store(0); decodeSkipped.store(0); maxDecodeUs.store(0);
	shown=displayDropped=lastClock=0;
	SceCtrlData pad={}; sceCtrlPeekBufferPositive(0,&pad,1); oldButtons=pad.buttons;
	audioPort=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,IntroMovieFile::SamplesPerFrame,
	                              IntroMovieFile::Rate,SCE_AUDIO_OUT_MODE_STEREO);
	IntroLog("[INTRO] PCM audio port: 0x%08X\n",audioPort);
	if(audioPort<0) return false;
	if(!StartThread(videoThread,"intro frames",DecodeThread,0x10000100)) return false;
	const SceUInt64 start=sceKernelGetProcessTimeWide();
	while(decoded.load()<std::min(4U,movie.Frames())) {
		if(failed.load() || sceKernelGetProcessTimeWide()-start>10000000) {
			IntroLog("[INTRO] Video prebuffer failed/timed out\n"); return false;
		}
		sceKernelDelayThread(1000);
	}
	IntroLog("[INTRO] First frames decoded; starting full soundtrack\n");
	if(!StartThread(audioThread,"intro PCM",AudioThread,0x100000F0)) return false;
	lastProgress=sceKernelGetProcessTimeWide();
	return true;
}

int VitaBootMoviePoll(uint32_t *pixels)
{
	SceCtrlData pad={};
	if(sceCtrlPeekBufferPositive(0,&pad,1)>0) {
		const unsigned pressed=pad.buttons&~oldButtons; oldButtons=pad.buttons;
		if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_START)) { IntroLog("[INTRO] Skipped by player\n"); return -1; }
	}
	if(failed.load()) return -1;
	if(audioDone.load()) { IntroLog("[INTRO] Complete movie and soundtrack finished\n"); return -1; }
	const unsigned target=clockFrame.load();
	const SceUInt64 now=sceKernelGetProcessTimeWide();
	if(target!=lastClock) { lastClock=target; lastProgress=now; }
	else if(now-lastProgress>3000000) { IntroLog("[INTRO] Audio clock stalled\n"); return -1; }
	const int frame=frames.Present(target,pixels,displayDropped);
	if(frame<0) return 0;
	if(shown++==0) { IntroLog("[INTRO] First real movie image copied: frame=%d\n",frame); IntroLogFlush(); }
	return 1;
}

void VitaBootMovieClose()
{
	stop.store(true);
	const bool audioJoined=JoinThread(audioThread);
	const bool videoJoined=JoinThread(videoThread);
	retained=!audioJoined || !videoJoined;
	if(retained) return;
	if(audioPort>=0) { sceAudioOutReleasePort(audioPort); audioPort=-1; }
	IntroLog("[INTRO] Playback totals: decoded=%u shown=%u skipped=%u discarded=%u max_read_decode_us=%u\n",
	         decoded.load(),shown,decodeSkipped.load(),displayDropped,maxDecodeUs.load());
}

void VitaBootMovieRelease()
{
	if(retained) return;
	frames.Free(); movie.Close();
}
#endif
