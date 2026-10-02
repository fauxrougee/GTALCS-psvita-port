#ifdef PSP2

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/apputil.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <psp2/io/stat.h>
#include <vitaGL.h>
#include "vita.h"
#include "vita_loading.h"
#include "vita_perf.h"
#include "vita_log.h"

#ifndef VITA_DATA_DIR
#define VITA_DATA_DIR "ux0:data/reLCS/"
#endif

// newlib heap for the game; vitaGL takes the rest of the RAM for textures
// and buffers (plus the 128MB of CDRAM).
extern "C" int _newlib_heap_size_user = 256 * 1024 * 1024;

// The default 256KB main thread stack is too small for the game.
extern "C" unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

// vitaGL calls this optional startup routine from a separate archive object.
// Leaving it unstarted keeps is_splashscreen_active false, so the first draw
// does not wait for a splash thread. The GL/context initialization still runs.
extern "C" void
__wrap_invoke_splashscreen(void)
{
	VitaLoadingStart();
}

static bool
FileExists(const char *path)
{
	SceIoStat st;
	return sceIoGetstat(path, &st) >= 0;
}

void
VitaInit(void)
{
	const int armResult = scePowerSetArmClockFrequency(444);
	const int busResult = scePowerSetBusClockFrequency(222);
	const int gpuResult = scePowerSetGpuClockFrequency(222);
	const int xbarResult = scePowerSetGpuXbarClockFrequency(166);

	SceAppUtilInitParam initParam = {};
	SceAppUtilBootParam bootParam = {};
	sceAppUtilInit(&initParam, &bootParam);

	sceIoMkdir(VITA_DATA_DIR, 0777);
	chdir(VITA_DATA_DIR);

#ifdef RELCS_BENCHMARK
	// Own log, truncated at each run: the game's log.txt is left alone.
	sceIoMkdir(VITA_DATA_DIR "benchmark", 0777);
	const bool asyncLog = VitaLogInit(VITA_DATA_DIR "benchmark/log.txt");
	printf("[BENCH] reLCS Benchmark, results in %sbenchmark/\n", VITA_DATA_DIR);
#else
	const bool asyncLog = VitaLogInit(VITA_DATA_DIR "log.txt");
#endif
	printf("reLCS Vita, data in %s\n", VITA_DATA_DIR);
	printf("[VITA] Clock requests CPU/bus/GPU/xbar results=%d/%d/%d/%d; actual MHz=%d/%d/%d/%d\n",
	       armResult, busResult, gpuResult, xbarResult, scePowerGetArmClockFrequency(),
	       scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
	printf("[VITA] Recovery baseline 8bbd1f9; experimental render and 30/60 paths removed\n");
	printf("[VITA] Script tracing disabled; async logging=%d (128 KiB queue)\n", asyncLog);
	printf("[VITA] Lens droplets disabled; mission brake cleanup fixed\n");
#ifdef VITA_DETAILED_GL_PROFILE
	printf("[VITA] Detailed GL call profiling enabled\n");
#else
	printf("[VITA] Lightweight GL profiling enabled\n");
#endif
	printf("[VITA] Software frame limiter removed; cached lighting/reflection uniforms; active-uniform traversal\n");
	printf("[VITA] Renderer 01.16: typed custom uniform cache, persistent vertex layouts, indexed draw bounds, opaque world shaders, ordered particle batches (256 sprites)\n");
	VitaPerfInit();

	// vitaGL compiles shaders at runtime with the system's shader compiler.
	if(!FileExists("ur0:data/libshacccg.suprx") && !FileExists("ur0:data/external/libshacccg.suprx"))
		printf("ERROR: ur0:data/libshacccg.suprx is missing, extract it with ShaRKBR33D\n");
	// sceIo doesn't know about newlib's working directory: absolute path.
	if(!FileExists(VITA_DATA_DIR "models/gta3.img"))
		printf("ERROR: game data not found, copy it to %s\n", VITA_DATA_DIR);
	VitaFlushLog();
	VitaBeginStartupUpdateCheck();
}

unsigned int
VitaGetFreeMemory(void)
{
	struct mallinfo mi = mallinfo();
	return (unsigned int)(_newlib_heap_size_user - mi.uordblks);
}

static VitaPerformanceStats performanceStats = { -1.0f, { -1.0f, -1.0f, -1.0f }, 0, 0, 0, 0 };

const VitaPerformanceStats &
VitaGetPerformanceStats(void)
{
	return performanceStats;
}

static void
SampleMemoryStats(void)
{
	const struct mallinfo mi = mallinfo();
	performanceStats.heapUsed = mi.uordblks;
	performanceStats.heapTotal = _newlib_heap_size_user;
	performanceStats.graphicsUsed = performanceStats.graphicsTotal = 0;
	// EXTERNAL allocations already belong to the newlib heap: don't count twice.
	const vglMemType pools[] = { VGL_MEM_RAM, VGL_MEM_VRAM, VGL_MEM_PHYCONT, VGL_MEM_BUDGET };
	for(vglMemType pool : pools){
		const size_t total = vglMemTotal(pool);
		const size_t free = vglMemFree(pool);
		performanceStats.graphicsTotal += total;
		performanceStats.graphicsUsed += total > free ? total - free : 0;
	}
}

#ifdef RELCS_BENCHMARK
static bool frameSampling = true;

void VitaSetFrameSampling(bool on) { frameSampling = on; }
void VitaSampleMemoryNow(void) { SampleMemoryStats(); }
#endif

void
VitaRecordPresentedFrame(void)
{
#ifdef RELCS_BENCHMARK
	if(!frameSampling)
		return;
#endif
	static SceUInt64 lastSample;
	static unsigned int frames;
	static SceKernelSystemInfo previous = {};
	static bool havePreviousCPU;
	const SceUInt64 now = sceKernelGetSystemTimeWide();
	if(lastSample){
		frames++;
		if(now - lastSample < 500000)
			return;
		performanceStats.fps = frames * 1000000.0 / (now - lastSample);
	}

	SceKernelSystemInfo current = {};
	current.size = sizeof(current);
	const bool haveCPU = sceKernelGetSystemInfo(&current) >= 0;
	for(int i = 0; i < 3; i++){
		performanceStats.cpu[i] = -1.0f;
		if(haveCPU && havePreviousCPU && lastSample &&
		   (current.activeCpuMask & previous.activeCpuMask & (1U << i)) &&
		   current.cpuInfo[i].idleClock >= previous.cpuInfo[i].idleClock){
			const SceUInt64 idle = current.cpuInfo[i].idleClock - previous.cpuInfo[i].idleClock;
			const double load = 100.0 * (1.0 - (double)idle / (now - lastSample));
			performanceStats.cpu[i] = load < 0.0 ? 0.0f : load > 100.0 ? 100.0f : (float)load;
		}
	}
	previous = current;
	havePreviousCPU = haveCPU;

	SampleMemoryStats();
	frames = 0;
	lastSample = now;
}

// Times accumulate per frame, then go into the report window when the frame
// is a gameplay frame (VitaProfCommitFrame) or are dropped (menus, loading
// screens: VitaProfDiscardFrame, see vita_perf.cpp). Sections nest: the report
// indents a section by its depth, and parents already include their children.
struct ProfSection {
	char name[32];
	const char *key;	// usual caller string, compared before the name
	SceUInt64 start;
	SceUInt64 frameTotal, total, maxFrame;
	unsigned int frameCalls, calls;
	int depth;
	int parent;	// same name under different parents is a different measurement
};
#define MAX_PROF_SECTIONS 128
#define MAX_PROF_DEPTH 32
static ProfSection profSections[MAX_PROF_SECTIONS];
static int numProfSections;
static int profDepth;
static int profStack[MAX_PROF_DEPTH];
static unsigned int profFrameCalls, profCalls;	// begin/end pairs
static double profPairUs;

static ProfSection *
FindProfSection(const char *name, int parent)
{
	for(int i = 0; i < numProfSections; i++)
		if(profSections[i].parent == parent && profSections[i].key == name)
			return &profSections[i];
	for(int i = 0; i < numProfSections; i++)
		if(profSections[i].parent == parent && strcmp(profSections[i].name, name) == 0)
			return &profSections[i];
	if(numProfSections == MAX_PROF_SECTIONS)
		return nullptr;
	ProfSection *s = &profSections[numProfSections++];
	strncpy(s->name, name, sizeof(s->name) - 1);
	s->key = name;
	s->depth = profDepth;
	s->parent = parent;
	return s;
}

void
VitaProfBegin(const char *name)
{
	if(profDepth >= MAX_PROF_DEPTH){ profDepth++; return; }
	int parent = profDepth ? profStack[profDepth - 1] : -1;
	// Do not orphan children when their parent could not be recorded.
	ProfSection *s = profDepth && parent < 0 ? nullptr : FindProfSection(name, parent);
	profStack[profDepth++] = s ? int(s - profSections) : -1;
	if(s)
		s->start = sceKernelGetProcessTimeWide();
}

void
VitaProfEnd(const char *name)
{
	if(profDepth == 0) return;
	if(profDepth > MAX_PROF_DEPTH){ profDepth--; return; }
	int index = profStack[profDepth - 1];
	ProfSection *s = index >= 0 ? &profSections[index] : nullptr;
	// An unbalanced end must not close an unrelated caller.
	if(s && s->key != name && strcmp(s->name, name) != 0) return;
	profDepth--;
	if(!s) return;
	if(s->start){
		s->frameTotal += sceKernelGetProcessTimeWide() - s->start;
		s->frameCalls++;
		s->start = 0;
		profFrameCalls++;
	}
}

// Some paths leave a section open (Idle's goto popret after StartOfFrame).
static void
EndProfFrame(bool keep)
{
	for(int i = 0; i < numProfSections; i++){
		ProfSection *s = &profSections[i];
		if(keep){
			s->total += s->frameTotal;
			s->calls += s->frameCalls;
			if(s->frameTotal > s->maxFrame)
				s->maxFrame = s->frameTotal;
		}
		s->frameTotal = 0;
		s->frameCalls = 0;
		s->start = 0;
	}
	if(keep)
		profCalls += profFrameCalls;
	profFrameCalls = 0;
	profDepth = 0;
}

void VitaProfCommitFrame(void) { EndProfFrame(true); }
void VitaProfDiscardFrame(void) { EndProfFrame(false); }

double
VitaProfCalibrate(void)
{
	// Before any section exists: time 1000 pairs on one, then remove it.
	if(numProfSections != 0)
		return profPairUs;
	SceUInt64 start = sceKernelGetProcessTimeWide();
	for(int i = 0; i < 1000; i++){
		VitaProfBegin("VitaProfCalibrate");
		VitaProfEnd("VitaProfCalibrate");
	}
	profPairUs = (sceKernelGetProcessTimeWide() - start) / 1000.0;
	memset(profSections, 0, sizeof(profSections));
	numProfSections = 0;
	profFrameCalls = 0;
	profDepth = 0;
	return profPairUs;
}

static void
PrintProfChildren(int parent, int frames)
{
	for(int i = 0; i < numProfSections; i++){
		const ProfSection &s = profSections[i];
		if(s.parent != parent || !s.calls) continue;
		printf("[VITA]   %*s%-*s %8.2f ms/frame  max %7.2f  (%.1f calls/frame)\n",
		       2 * s.depth, "", 28 - 2 * s.depth, s.name,
		       s.total / 1000.0 / frames, s.maxFrame / 1000.0, double(s.calls) / frames);
		PrintProfChildren(i, frames);
	}
}

void
VitaProfReportSections(int frames)
{
	if(frames > 0){
		printf("[VITA]   sections, ms/frame over %d gameplay frames (children indented, included in parents):\n", frames);
		PrintProfChildren(-1, frames);
		printf("[PERF] profiler pairs_per_frame=%.1f est_overhead_ms=%.3f\n",
		       (double)profCalls / frames, profCalls * profPairUs / 1000.0 / frames);
	}
	for(int i = 0; i < numProfSections; i++){
		ProfSection *s = &profSections[i];
		s->total = 0;
		s->calls = 0;
		s->maxFrame = 0;
	}
	profCalls = 0;
}

static const ProfSection *
ProfSectionAt(int i)
{
	return i >= 0 && i < numProfSections ? &profSections[i] : nullptr;
}

int VitaProfSectionCount(void) { return numProfSections; }

const char *
VitaProfSectionName(int i)
{
	const ProfSection *s = ProfSectionAt(i);
	return s ? s->name : "";
}

int
VitaProfSectionParent(int i)
{
	const ProfSection *s = ProfSectionAt(i);
	return s ? s->parent : -1;
}

int
VitaProfSectionDepth(int i)
{
	const ProfSection *s = ProfSectionAt(i);
	return s ? s->depth : 0;
}

unsigned int
VitaProfSectionFrameUs(int i)
{
	const ProfSection *s = ProfSectionAt(i);
	return s ? (unsigned int)s->frameTotal : 0;
}

unsigned int
VitaProfSectionFrameCalls(int i)
{
	const ProfSection *s = ProfSectionAt(i);
	return s ? s->frameCalls : 0;
}

const char *
VitaGetSystemLocale(void)
{
	int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
	switch(lang){
	case SCE_SYSTEM_PARAM_LANG_FRENCH: return "fr_FR";
	case SCE_SYSTEM_PARAM_LANG_GERMAN: return "de_DE";
	case SCE_SYSTEM_PARAM_LANG_ITALIAN: return "it_IT";
	case SCE_SYSTEM_PARAM_LANG_SPANISH: return "es_ES";
	case SCE_SYSTEM_PARAM_LANG_ENGLISH_GB: return "en_GB";
	default: return "en_US";
	}
}

#endif
