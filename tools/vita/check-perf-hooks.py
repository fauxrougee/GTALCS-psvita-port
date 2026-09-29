#!/usr/bin/env python3
"""Test production frame hooks and section accounting with a controlled OS clock.

Vita/GPU calls are mocked. This checks instrumentation and limiter decisions,
not real display timing or achieved device FPS. Native Windows uses MSVC/ASan;
other hosts use GCC/ASan/UBSan. WSL is never launched by this script.
An optional source root can replay these checks against a preserved snapshot.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT
SRC = SOURCE_ROOT / 'src/skel/vita'

def strip(path):
    return '\n'.join(l for l in path.read_text().splitlines() if not l.startswith('#include'))

vita = (SRC / 'vita.cpp').read_text()
sections = vita[vita.index('// Times accumulate per frame'):vita.index('const char *\nVitaGetSystemLocale')]
MOCKS = r'''
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <malloc.h>
#include <string>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
#include "vita.h"
#include "vita_perf.h"
#include "vita_perf_stats.h"
#include "vita_log.h"
using SceUID=int; using SceUInt=unsigned; using SceUInt64=uint64_t;
struct SceKernelThreadInfo {unsigned size; uint64_t runClocks;};
struct SceKernelSystemInfo {unsigned size,activeCpuMask; struct {uint64_t idleClock;} cpuInfo[4];};
using vglMemType=int;
constexpr int VGL_MEM_RAM=0,VGL_MEM_VRAM=1,VGL_MEM_PHYCONT=2,VGL_MEM_BUDGET=3;
static uint64_t clockUs=1000000;
static std::string logText;
static void (*displayCallback)(void*);
uint64_t sceKernelGetProcessTimeWide(){return clockUs;}
int sceKernelDelayThread(unsigned n){clockUs+=n; return 0;}
int sceKernelGetThreadId(){return 1;}
int sceKernelGetThreadInfo(int,SceKernelThreadInfo *s){s->runClocks=clockUs/2;return 0;}
int sceKernelGetSystemInfo(SceKernelSystemInfo *s){s->activeCpuMask=7;for(auto &c:s->cpuInfo)c.idleClock=clockUs/2;return 0;}
int sceDisplayGetVcount(){return clockUs/16667;}
void vglSetDisplayCallback(void (*cb)(void*)){displayCallback=cb;}
size_t vglMemFree(int){return 50*1024*1024;}
size_t vglMemTotal(int){return 64*1024*1024;}
void VitaGLProfSetCounting(bool){}
void VitaGLProfCommitFrame(){}
void VitaGLProfDiscardFrame(){}
void VitaGLProfReport(int){}
void VitaFlushLog(){clockUs+=50000;}
VitaLogStats VitaLogGetStats(){return {};}
const VitaPerformanceStats &VitaGetPerformanceStats(){static VitaPerformanceStats s={};return s;}
extern "C" {
void *__real_malloc(size_t n){return malloc(n);}
void *__real_calloc(size_t n,size_t s){return calloc(n,s);}
void *__real_realloc(void *p,size_t n){return realloc(p,n);}
void *__real_memalign(size_t,size_t n){return malloc(n);} // alignment is not exercised here
void __real_free(void *p){free(p);}
}
int TestPrintf(const char *format,...){
    char b[4096];va_list ap;va_start(ap,format);int n=vsnprintf(b,sizeof(b),format,ap);va_end(ap);
    assert(n>=0 && n<int(sizeof(b)));logText.append(b,n);return n;
}
#define printf TestPrintf
'''
TESTS = r'''
static void section(const char *name,unsigned us){VitaProfBegin(name);clockUs+=us;VitaProfEnd(name);}
static void testSections(){
    // This name was first seen outside gameplay, before any parent exists.
    section("Shared",50000);VitaProfDiscardFrame();
    VitaProfBegin("ParentA");clockUs+=1000;section("Shared",2000);VitaProfEnd("ParentA");
    VitaProfBegin("ParentB");clockUs+=3000;section("Shared",4000);VitaProfEnd("ParentB");
    // A mismatched end must leave its real caller open.
    VitaProfBegin("Balanced");VitaProfEnd("Missing");clockUs+=5000;VitaProfEnd("Balanced");
    VitaProfCommitFrame();VitaProfReportSections(1);
    auto a=logText.find("ParentA"),b=logText.find("ParentB");
    auto childA=logText.find("Shared"),childB=logText.find("Shared",childA+1);
    assert(a<childA && childA<b && b<childB);
    assert(logText.find("Balanced")!=std::string::npos);
    fputs(logText.c_str(),stdout);
    // Deep recursion and full tables must stay bounded.
    for(unsigned i=0;i<80;++i)VitaProfBegin("Deep");
    for(unsigned i=0;i<80;++i)VitaProfEnd("Deep");
    VitaProfDiscardFrame();
    static char names[200][32];
    for(unsigned i=0;i<200;++i){snprintf(names[i],sizeof(names[i]),"section%u",i);section(names[i],10);}
    VitaProfDiscardFrame();
}
static void testLimiter(){
    // Missing INI and old frame-limit keys must both start uncapped. Test the
    // actual parser, including that report options remain configurable.
    VitaPerfInit(); assert(settings.frameLimit==-1);
    for(int cap : {0,20,30,45,60,-1,999}){
        FILE *f=fopen("reLCS.ini","w"); assert(f);
        fprintf(f,"[VitaPerf]\n FrameLimit = %d\n ReportSeconds = 7\n GLCounters=0\n",cap);
        fclose(f); ReadSettings();
        assert(settings.frameLimit==-1 && settings.reportSeconds==7 && !settings.glCounters);
        for(bool limiter : {false,true}) for(bool vsync : {false,true}){
            uint64_t before=clockUs;
            for(float elapsed : {0.0f,1.0f,16.0f,22.0f,33.0f,34.0f,100.0f})
                assert(VitaPerfFrameDue(limiter,vsync,elapsed,30));
            assert(clockUs==before); // must not sleep to enforce a deadline
            for(bool menu : {false,true}){
                VitaPerfIdleBegin(menu);
                assert(VitaPerfSwapInterval(0)==0);
                assert(VitaPerfSwapInterval(1)==1 && VitaPerfSwapInterval(2)==1);
                VitaPerfIdleEnd(0,menu);
            }
        }
    }
    remove("reLCS.ini");
    assert(!strcmp(LimitName(),"uncapped"));
}
static void testFrames(){
    settings.reportSeconds=1;VitaPerfGLReady();
    for(unsigned i=0;i<220;++i){
        VitaPerfIdleBegin(false);clockUs+=10000;VitaPerfSwapBegin();clockUs+=1000;
        VitaPerfSwapEnd();displayCallback(nullptr);VitaPerfIdleEnd(clockUs/1000,false);
        clockUs+=5000;
    }
    // I/O cost must still appear in observed intervals, not be subtracted.
    assert(windowIndex>=3 && logText.find("max=66.00")!=std::string::npos);
    assert(logText.find("report_included=50.00")!=std::string::npos);
    assert(logText.find("report_excluded=")==std::string::npos);
    VitaPerfIdleBegin(true);VitaPerfSwapBegin();clockUs+=5000;VitaPerfSwapEnd();VitaPerfIdleEnd(0,true);
    assert(!havePrevious);
    clockUs+=2000000;
    VitaPerfIdleBegin(false);clockUs+=1000;VitaPerfSwapBegin();clockUs+=1000;VitaPerfSwapEnd();VitaPerfIdleEnd(0,false);
    assert(intervals.Count()==0); // time spent in the menu cannot contaminate gameplay
    VitaPerfSwapBegin();clockUs+=1000;VitaPerfSwapEnd(); // standalone loading frame
    VitaPerfIdleBegin(false);clockUs+=1000;VitaPerfSwapBegin();clockUs+=1000;VitaPerfSwapEnd();VitaPerfIdleEnd(0,false);
    assert(intervals.Count()==0);
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"sections"))testSections();
    else if(!strcmp(argv[1],"limiter"))testLimiter();
    else if(!strcmp(argv[1],"frames"))testFrames();
    else assert(false);
    puts("PASS hook scenario");
}
'''

with tempfile.TemporaryDirectory(prefix='relcs-perf-hooks-') as tmp:
    tmp = Path(tmp)
    cpp = tmp/'hooks.cpp'; exe = tmp/'hooks'
    cpp.write_text(MOCKS + sections + strip(SRC/'vita_perf.cpp') + TESTS)
    exe, env = build(exe, [cpp, SRC/'vita_perf_stats.cpp'], [SRC],
                     ['PSP2','VITA_PERF_HOST_TEST','VITA_DATA_DIR=""'])
    output = ''
    for scenario in ['sections','limiter','frames']:
        result = run(exe, env, [scenario])
        print('PASS:',scenario)
        if scenario=='sections': output=result

    spec=importlib.util.spec_from_file_location('report',ROOT/'tools/vita/perf-report.py')
    report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
    rows=[]
    for line in output.splitlines():
        m=report.SECTION.match(line)
        if m:rows.append((len(m[1])//2,m[2],float(m[3]),float(m[4])))
    assert report.exclusive(rows)=={'ParentA':1.0,'Shared':6.0,'ParentB':3.0,'Balanced':5.0},rows
    print('PASS: real report parser, correct hierarchy and repeated names')
