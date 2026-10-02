#!/usr/bin/env python3
"""Run the actual launcher + software player with real movie data and real threads.

Only Vita OS calls are simulated. PCM output is paced at its real 48-kHz rate;
the full 111-second run checks complete decoding/audio and frame accounting,
then launch of the unchanged game. Host scheduler delays may discard obsolete
display frames, as intended by the player; host timing is not Vita performance.
"""
from pathlib import Path
import sys
import argparse
import os
import shutil
from host_build import build, run
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
LAUNCH=ROOT/'vita/launcher'


def stripped(path):
    return '\n'.join(line for line in path.read_text().splitlines() if not line.startswith('#include'))


SOURCE=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>
#include "movie_file.h"
#include "movie_queue.h"
#include "vita_boot_movie.h"
using SceUID=int; using SceSize=unsigned; using SceUInt=unsigned; using SceUInt64=uint64_t;
using SceDisplaySetBufSync=unsigned;
using SceKernelThreadEntry=int(*)(SceSize,void*);
constexpr unsigned SCE_KERNEL_CPU_MASK_USER_ALL=0x70000;
constexpr int SCE_AUDIO_OUT_PORT_TYPE_MAIN=0,SCE_AUDIO_OUT_MODE_STEREO=1;
constexpr int SCE_CTRL_MODE_DIGITAL=0,SCE_KERNEL_POWER_TICK_DEFAULT=0;
constexpr unsigned SCE_CTRL_CROSS=0x4000,SCE_CTRL_START=8;
constexpr unsigned SCE_CTRL_CIRCLE=0x2000;
constexpr unsigned SCE_DISPLAY_SETBUF_IMMEDIATE=0,SCE_DISPLAY_SETBUF_NEXTFRAME=1;
constexpr int SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW=1,SCE_DISPLAY_PIXELFORMAT_A8B8G8R8=0;
struct SceCtrlData { unsigned buttons; };
struct SceIoStat { uint64_t st_size; };
struct SceAppUtilInitParam {};
struct SceAppUtilBootParam {};
struct SceAppUtilAppEventParam { int type=0; };
struct SceDisplayFrameBuf { unsigned size; void *base; unsigned pitch,width,height,pixelformat; };
static const auto epoch=std::chrono::steady_clock::now();
static std::string scenario;
static unsigned controllerReads,gameLaunches,displayPresentations,audioBlocks;
static unsigned long pcmCrc=0;
static bool audioOpen,displayFreed;
static void *displayMemory,*scanout,*queuedScanout;
static auto audioDeadline=epoch;
struct MockThread { std::thread worker; SceKernelThreadEntry entry=nullptr; std::string name; std::atomic<bool> ended{false}; };
static MockThread workers[4];
static unsigned nextThread=0;

bool IntroLogInit(){ return true; }
void IntroLog(const char *fmt,...){ va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap); fflush(stdout); }
void IntroLogFlush(){ fflush(stdout); }
SceUInt64 sceKernelGetProcessTimeWide(){
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-epoch).count();
}
int sceKernelDelayThread(unsigned us){ std::this_thread::sleep_for(std::chrono::microseconds(us)); return 0; }
int scePowerSetArmClockFrequency(int mhz){ assert(mhz==444); return 0; }
int sceCtrlSetSamplingMode(int){ return 0; }
int sceCtrlPeekBufferPositive(int,SceCtrlData *p,int){
    ++controllerReads; p->buttons=0;
    if(scenario=="skip" && controllerReads>20) p->buttons=SCE_CTRL_START;
    if(scenario=="held-skip" && (controllerReads<12 || controllerReads>45)) p->buttons=SCE_CTRL_CROSS;
    return 1;
}
int sceAppUtilInit(SceAppUtilInitParam*,SceAppUtilBootParam*) { return 0; }
int sceAppUtilShutdown() { return 0; }
int sceAppUtilReceiveAppEvent(SceAppUtilAppEventParam *e) { e->type=scenario.find("update")==0?5:0; return 0; }
int sceAppUtilAppEventParseLiveArea(SceAppUtilAppEventParam*,char *p) { strcpy(p,"-update"); return 0; }
namespace Update {
bool OpenScreen() { return true; }
void CloseScreen() {}
int StartHelper() { return scenario=="update-fail"?-123:0; }
void Screen(const std::string&,const std::string&) {}
unsigned Buttons() { return SCE_CTRL_CIRCLE; }
}
int sceAudioOutOpenPort(int type,int len,int rate,int mode){
    assert(type==0 && len==1920 && rate==48000 && mode==1);
    if(scenario=="audio-open") return -11;
    audioOpen=true; return 7;
}
int sceAudioOutOutput(int port,const void *buf){
    assert(port==7 && audioOpen);
    if(!buf){ std::this_thread::sleep_until(audioDeadline); return 0; }
    if(scenario=="audio-output" && audioBlocks==5) return -22;
    if(scenario=="stall" && audioBlocks==2) sceKernelDelayThread(3200000);
    if(audioBlocks) std::this_thread::sleep_until(audioDeadline);
    else audioDeadline=std::chrono::steady_clock::now();
    audioDeadline+=std::chrono::milliseconds(40);
    pcmCrc=crc32(pcmCrc,static_cast<const unsigned char*>(buf),7680);
    ++audioBlocks;
    return 0;
}
int sceAudioOutReleasePort(int p){
    assert(p==7 && audioOpen);
    if(audioBlocks) assert(std::chrono::steady_clock::now()>=audioDeadline);
    audioOpen=false; return 0;
}
int sceKernelCreateThread(const char *name,SceKernelThreadEntry entry,int,unsigned size,unsigned,unsigned affinity,void*){
    assert(size==65536 && affinity==SCE_KERNEL_CPU_MASK_USER_ALL);
    if(scenario=="video-create" && !strcmp(name,"intro frames")) return -33;
    if(scenario=="audio-create" && !strcmp(name,"intro PCM")) return -33;
    assert(nextThread<4); auto &t=workers[nextThread]; t.name=name; t.entry=entry;
    return int(nextThread++);
}
int sceKernelStartThread(int id,unsigned,void*){
    auto &t=workers[id];
    if(scenario=="video-start" && t.name=="intro frames") return -44;
    if(scenario=="audio-start" && t.name=="intro PCM") return -44;
    t.worker=std::thread([&t]{ t.entry(0,nullptr); t.ended.store(true); }); return 0;
}
int sceKernelWaitThreadEnd(int id,int*,unsigned *timeout){
    auto &t=workers[id];
    if(scenario=="join-fail") return -55;
    const auto until=std::chrono::steady_clock::now()+std::chrono::microseconds(*timeout);
    while(!t.ended.load()) {
        if(std::chrono::steady_clock::now()>until) return -66;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}
int sceKernelDeleteThread(int id){ auto &t=workers[id]; if(t.worker.joinable()) t.worker.join(); return 0; }
int sceIoMkdir(const char*,int){ return 0; }
int sceIoGetstat(const char*,SceIoStat *s){ s->st_size=277992418; return 0; }
int sceKernelAllocMemBlock(const char*,int,unsigned size,void*){
    if(scenario=="display-alloc") return -77;
    displayMemory=malloc(size); assert(displayMemory); return 4;
}
int sceKernelGetMemBlockBase(int id,void **out){ assert(id==4); *out=displayMemory; return 0; }
int sceKernelFreeMemBlock(int id){
    assert(id==4 && !scanout && !queuedScanout);
    free(displayMemory); displayMemory=nullptr; displayFreed=true; return 0;
}
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *fb,unsigned){
    if(fb && scenario=="display-present") return -88;
    if(fb){
        assert(fb->size==sizeof(*fb) && fb->pitch==960 && fb->width==960 && fb->height==544);
        ++displayPresentations; queuedScanout=fb->base;
        auto *p=static_cast<uint32_t*>(fb->base);
        for(unsigned y=0;y<544;++y) for(unsigned x : {0U,159U,800U,959U}) assert(p[y*960+x]==0xff000000);
    }else queuedScanout=nullptr;
    return 0;
}
int sceDisplayWaitVblankStartMulti(unsigned n){
    sceKernelDelayThread(16667*n); scanout=queuedScanout;
    static uint64_t next=20000000;
    if(sceKernelGetProcessTimeWide()>next){ printf("Playback progress: %.1f s\n",next/1e6); fflush(stdout); next+=20000000; }
    return 0;
}
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *fb,unsigned sync){ fb->base=sync?queuedScanout:scanout; return 0; }
int sceKernelPowerTick(int){ return 0; }
int sceAppMgrLoadExec(const char *p,void*,void*){
    assert(!strcmp(p,"app0:game.bin")); ++gameLaunches;
    assert(!scanout && !queuedScanout);
    if(scenario!="join-fail"){
        assert(!audioOpen);
        for(unsigned n=0;n<nextThread;++n) assert(!workers[n].worker.joinable());
    }
    return -99;
}
int sceKernelExitProcess(int result){ assert(result==-99 || (scenario=="update"&&result==0) || (scenario=="update-fail"&&result==-123)); return 0; }
'''
core=(ROOT/'vita/updater/core.cpp').read_text()
SOURCE+='namespace Update {\n'+core[core.index('bool LaunchUpdate('):core.index('bool SafePath(')]+'}\n'
SOURCE+='\n'+stripped(LAUNCH/'movie.cpp')+'\n'
SOURCE+=stripped(LAUNCH/'main.cpp').replace('int main()', 'int LauncherMain()')+'\n'
SOURCE+=r'''
int main(int argc,char **argv){
    assert(argc==2); scenario=argv[1];
    if(scenario=="update" || scenario=="update-fail") {
        assert(LauncherMain()==(scenario=="update"?0:-123));
        assert(!gameLaunches && !audioBlocks && !displayPresentations && !nextThread);
        puts("PASS: LiveArea update launches the helper without movie or game initialization");
        return 0;
    }
    // join-fail uses the same early skip as "skip", then simulates failed joins.
    if(scenario=="join-fail") {
        std::thread stopper([]{ std::this_thread::sleep_for(std::chrono::milliseconds(500)); failed.store(true); });
        assert(LauncherMain()==-99); stopper.join();
        assert(retained && movie.Audio());
        scenario="cleanup"; VitaBootMovieClose(); VitaBootMovieRelease();
    }else assert(LauncherMain()==-99);
    assert(gameLaunches==1 && !audioOpen);
    if(scenario=="full"){
        assert(audioBlocks==2774 && decoded.load()==2774 && decodeSkipped.load()==0);
        // Wall-clock host scheduling is not a guaranteed 60 Hz display. Every
        // decoded frame must be displayed or explicitly counted as obsolete.
        assert(shown>0 && shown+displayDropped==2774);
        // Compare the exact audio CRC carried in the validated release header.
        FILE *f=fopen("app0:boot/intro.vtm","rb"); assert(f);
        unsigned char header[80]; assert(fread(header,1,80,f)==80); fclose(f);
        unsigned expected=uint32_t(header[64])|uint32_t(header[65])<<8|uint32_t(header[66])<<16|uint32_t(header[67])<<24;
        assert(pcmCrc==expected);
        assert(sceKernelGetProcessTimeWide()>=110960000);
    }else assert(audioBlocks<2774);
    assert(!movie.Audio() && (displayFreed || scenario=="display-alloc"));
    printf("PASS scenario=%s images=%u obsolete=%u PCM_blocks=%u game_launches=%u\n",scenario.c_str(),shown,displayDropped,audioBlocks,gameLaunches);
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--media',type=Path,default=ROOT/'build/vita-intro/intro.vtm')
    parser.add_argument('cases',nargs='*')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='relcs-playback-') as temp:
        temp=Path(temp)
        src=temp/'check.cpp'
        src.write_text(SOURCE.replace('app0:', 'app0-').replace('ux0:', 'ux0-'))
        exe=temp/'check'
        exe,env=build(exe,[src,LAUNCH/'movie_file.cpp',LAUNCH/'lz4/lz4.c'],[ROOT/'tools/vita/test_support',LAUNCH],['PSP2'])
        media=temp/'app0-boot'
        media.mkdir()
        try:
            os.link(args.media.resolve(),media/'intro.vtm')
        except OSError:
            shutil.copy2(args.media,media/'intro.vtm')
        cases=args.cases or ['full','skip','held-skip','audio-open','video-create','audio-create',
                         'video-start','audio-start','audio-output','display-alloc','display-present',
                         'stall','join-fail','update','update-fail']
        for scenario in cases:
            subprocess.run([str(exe),scenario],cwd=temp,env=env,check=True,timeout=180)
        (media/'intro.vtm').unlink()
        subprocess.run([str(exe),'missing'],cwd=temp,env=env,check=True,timeout=10)


if __name__=='__main__':
    main()
