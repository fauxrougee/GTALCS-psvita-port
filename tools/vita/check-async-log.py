#!/usr/bin/env python3
"""Exercise the production logger with real host threads and controlled Vita I/O.

Run natively on Windows (MSVC/ASan) or Linux (GCC/ASan/UBSan).
A blocked storage writer must not block frame-side printf/flush. Sanitizers
cover bounds, queue wrap, full queues, short writes and shutdown.
This is not a measurement of Vita FPS or of its scheduler/storage driver.
"""
from pathlib import Path
import subprocess
import tempfile
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'src/skel/vita'
MOCKS = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
using SceUID=int; using SceSize=unsigned; using SceUInt=unsigned;
constexpr int SCE_O_WRONLY=1,SCE_O_CREAT=2,SCE_O_TRUNC=4,SCE_KERNEL_CPU_MASK_USER_ALL=7;
using Clock=std::chrono::steady_clock;
std::mutex qMutex,semMutex,ioMutex,endMutex;
std::condition_variable semCV,ioCV,endCV;
std::thread worker;
std::atomic<bool> blockIO(false),ioEntered(false),ended(false),failIO(false);
std::atomic<unsigned> ioCalls(0);
int signalCount=0,failStage=0,closed=0,registered=0;
std::string output;
const auto mainThread=std::this_thread::get_id();
int (*entry)(SceSize,void*);
void VitaPerfRegisterThread(const char *name){assert(!strcmp(name,"LogWriter"));}
uint64_t sceKernelGetProcessTimeWide(){return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();}
int sceIoOpen(const char *,int,int){return failStage==1?-1:1;}
int sceIoClose(int f){assert(f==1);++closed;return 0;}
int sceIoWrite(int f,const void *data,unsigned n){
    assert(f==1 && std::this_thread::get_id()!=mainThread);
    std::unique_lock<std::mutex> lock(ioMutex);ioEntered=true;ioCV.notify_all();
    ioCV.wait(lock,[]{return !blockIO.load();});
    ++ioCalls;
    if(failIO) return -1;
    n=std::min(n,113u); // exercise partial writes of every batch
    output.append(static_cast<const char*>(data),n);return int(n);
}
int sceKernelCreateMutex(const char *,unsigned,int,void*){return failStage==2?-1:2;}
int sceKernelLockMutex(int f,int,void*){assert(f==2);qMutex.lock();return 0;}
int sceKernelUnlockMutex(int f,int){assert(f==2);qMutex.unlock();return 0;}
int sceKernelDeleteMutex(int f){assert(f==2);return 0;}
int sceKernelCreateSema(const char *,unsigned,int,int,void*){return failStage==3?-1:3;}
int sceKernelSignalSema(int f,int){assert(f==3);std::lock_guard<std::mutex> l(semMutex);if(signalCount)return -1;signalCount=1;semCV.notify_one();return 0;}
int sceKernelWaitSema(int f,int,unsigned *timeout){
    assert(f==3);std::unique_lock<std::mutex> l(semMutex);
    if(!semCV.wait_for(l,std::chrono::microseconds(*timeout),[]{return signalCount!=0;}))return -1;
    signalCount=0;return 0;
}
int sceKernelDeleteSema(int f){assert(f==3);return 0;}
int sceKernelCreateThread(const char *,int (*e)(SceSize,void*),int,unsigned,unsigned,int,void*){entry=e;return failStage==4?-1:4;}
int sceKernelStartThread(int f,unsigned,void*){
    assert(f==4);if(failStage==5)return -1;
    worker=std::thread([]{entry(0,nullptr);{std::lock_guard<std::mutex> l(endMutex);ended=true;}endCV.notify_all();});return 0;
}
int sceKernelWaitThreadEnd(int f,void*,unsigned *timeout){
    assert(f==4);std::unique_lock<std::mutex> l(endMutex);
    if(!endCV.wait_for(l,std::chrono::microseconds(*timeout),[]{return ended.load();}))return -1;
    worker.join();return 0;
}
int sceKernelDeleteThread(int f){assert(f==4);return 0;}
int TestAtExit(void (*)()){++registered;return 0;}
extern "C" {
int __real_vprintf(const char *f,va_list ap){return vprintf(f,ap);}
int __real_puts(const char *s){return puts(s);}
int __real_putchar(int c){return putchar(c);}
int __real_vfprintf(FILE *s,const char *f,va_list ap){return vfprintf(s,f,ap);}
int __real_fputs(const char *s,FILE *f){return fputs(s,f);}
int __real_fputc(int c,FILE *f){return fputc(c,f);}
size_t __real_fwrite(const void *p,size_t s,size_t n,FILE *f){return fwrite(p,s,n,f);}
int __real_fflush(FILE *f){return fflush(f);}
}
'''
TESTS = r'''
void releaseIO(){ {std::lock_guard<std::mutex> l(ioMutex);blockIO=false;}ioCV.notify_all(); }
void awaitIO(){std::unique_lock<std::mutex> l(ioMutex);assert(ioCV.wait_for(l,std::chrono::seconds(2),[]{return ioEntered.load();}));}
void finish(){VitaLogShutdown();assert(!VitaLogGetStats().running && closed==1 && !worker.joinable());}
void regularFile(){
    FILE *f=tmpfile();assert(f);
    assert(__wrap_fprintf(f,"save:%d",42)==7);
    assert(__wrap_fputs("!",f)>=0 && __wrap_fputc('?',f)=='?');
    assert(__wrap_fwrite("END",1,3,f)==3 && __wrap_fflush(f)==0);
    rewind(f);char b[32]={};assert(fread(b,1,31,f)==12);assert(!strcmp(b,"save:42!?END"));fclose(f);
}
void fullQueue(){
    blockIO=true;assert(VitaLogInit("log"));
    __wrap_puts("first");VitaFlushLog();awaitIO();
    // The disk is deliberately blocked. These producers and flushes must
    // complete BEFORE it is released; a synchronous path deadlocks the test.
    std::string big(CAPACITY,'Q');
    assert(__wrap_fwrite(big.data(),1,big.size(),stdout)==CAPACITY);
    assert(__wrap_puts("")==EOF); // full queue + newline: no unsigned underflow
    assert(__wrap_putchar('x')==EOF);
    assert(__wrap_fwrite("x",size_t(-1),2,stderr)==0); // multiplication overflow
    for(int i=0;i<100;++i)VitaFlushLog();
    auto s=VitaLogGetStats();assert(s.queued==CAPACITY && s.peak==CAPACITY && s.dropped==2);
    releaseIO();finish();assert(output=="first\n"+big && ioCalls>1);
}
void concurrent(){
    assert(VitaLogInit("log"));regularFile();
    constexpr int producers=4, rounds=12, lines=200;
    for(int r=0;r<rounds;++r){
        std::vector<std::thread> ts;
        for(int p=0;p<producers;++p)ts.emplace_back([=]{for(int i=0;i<lines;++i)assert(__wrap_printf("R%02dP%dL%03d abcdefghijklmnop\n",r,p,i)>0);});
        for(auto &t:ts)t.join();
        VitaFlushLog();
        auto deadline=Clock::now()+std::chrono::seconds(2);
        while(VitaLogGetStats().queued){assert(Clock::now()<deadline);std::this_thread::yield();}
    }
    finish();assert(!VitaLogGetStats().dropped && !VitaLogGetStats().errors);
    std::set<std::string> seen;size_t begin=0,end;
    while((end=output.find('\n',begin))!=std::string::npos){assert(seen.insert(output.substr(begin,end-begin)).second);begin=end+1;}
    assert(begin==output.size() && seen.size()==producers*rounds*lines);
    for(int r=0;r<rounds;++r)for(int p=0;p<producers;++p)for(int i=0;i<lines;++i){
        char line[100];snprintf(line,sizeof(line),"R%02dP%dL%03d abcdefghijklmnop",r,p,i);assert(seen.count(line));
    }
}
void formatting(){
    assert(VitaLogInit("log"));
    __wrap_fprintf(stderr,"error %d\n",7);__wrap_fputs("abc",stderr);__wrap_fputc('!',stderr);__wrap_putchar('\n');
    std::string longLine(5000,'z');assert(__wrap_printf("%s",longLine.c_str())==2047);
    assert(__wrap_fflush(stderr)==0);finish();
    assert(output.substr(0,12)=="error 7\nabc!" && output.find("[LOG truncated]\n")!=std::string::npos);
    assert(VitaLogGetStats().truncated==1 && !VitaLogGetStats().errors);
}
void failedWrite(){
    failIO=true;assert(VitaLogInit("log"));__wrap_puts("discarded");VitaFlushLog();finish();
    auto s=VitaLogGetStats();assert(s.errors==1 && s.dropped==10 && output.empty());
}
void shutdownInFlight(){
    blockIO=true;assert(VitaLogInit("log"));__wrap_puts("pending");VitaFlushLog();awaitIO();
    VitaLogShutdown(); // 2 s timeout retains everything still used by the writer
    assert(VitaLogGetStats().running && closed==0 && worker.joinable());
    assert(__wrap_puts("too late")==EOF);releaseIO();finish();assert(output=="pending\n");
}
int main(int argc,char **argv){
    assert(argc==2);std::string scenario=argv[1];
    if(scenario=="full")fullQueue();
    else if(scenario=="concurrent")concurrent();
    else if(scenario=="formatting")formatting();
    else if(scenario=="write_failure")failedWrite();
    else if(scenario=="shutdown")shutdownInFlight();
    else {failStage=atoi(argv[1]);assert(failStage>=1 && failStage<=5);assert(!VitaLogInit("log"));assert(!VitaLogGetStats().running);assert(closed==(failStage==1?0:1) && !registered);}
    puts("PASS async log scenario");
}
'''

with tempfile.TemporaryDirectory(prefix='relcs-async-log-') as temp:
    tmp = Path(temp)
    cpp, exe = tmp/'log.cpp', tmp/'log'
    code = '\n'.join(line for line in (SRC/'vita_log.cpp').read_text().splitlines()
                     if not line.startswith('#include <psp2/') and line != '#include "vita_perf.h"')
    code = code.replace('atexit(VitaLogShutdown)', 'TestAtExit(VitaLogShutdown)')
    cpp.write_text(MOCKS + code + TESTS)
    exe, env = build(exe, [cpp], [SRC], ['PSP2'])
    for scenario in ['full', 'concurrent', 'formatting', 'write_failure', 'shutdown', '1','2','3','4','5']:
        run(exe, env, [scenario])
        print('PASS:', scenario)
