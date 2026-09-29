#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include "intro_log.h"

namespace {
SceUID logFd=-1;
std::atomic_flag logLock=ATOMIC_FLAG_INIT;
void Lock() { while(logLock.test_and_set(std::memory_order_acquire)) sceKernelDelayThread(1000); }
void Unlock() { logLock.clear(std::memory_order_release); }
}

bool IntroLogInit()
{
	// Use a real Vita file descriptor. stdout is handled specially by the Vita
	// C runtime; freopen created a file but did not capture our console output.
	logFd=sceIoOpen("ux0:data/reLCS/intro.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
	if(logFd<0) return false;
	IntroLog("[INTRO] Diagnostic logger v2 opened\n");
	IntroLogFlush();
	return true;
}

void IntroLog(const char *format,...)
{
	if(logFd<0) return;
	char text[768];
	va_list args; va_start(args,format);
	int count=vsnprintf(text,sizeof(text),format,args);
	va_end(args);
	if(count<=0) return;
	if(count>=static_cast<int>(sizeof(text))) count=sizeof(text)-1;
	Lock();
	for(int offset=0; offset<count;){
		const int written=sceIoWrite(logFd,text+offset,count-offset);
		if(written<=0) break;
		offset+=written;
	}
	Unlock();
}

void IntroLogFlush()
{
	if(logFd<0) return;
	Lock(); sceIoSyncByFd(logFd,0); Unlock();
}
