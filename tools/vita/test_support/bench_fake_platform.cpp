// Deterministic BenchPlatform for the host test of benchmark_metrics.cpp
// (tools/vita/check-bench-metrics.py). The clock, counters, profiler sections
// and GPU completions are driven by the test through the Fake* functions;
// result files go to the folder given to FakeSetOutDir.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "benchmark_platform.h"

static uint64_t gNow;
static uint32_t gCaps;
static char gRoot[512] = "bench-out/";
static BenchCounters gCounters;
static uint32_t gSyncWait;
static uint32_t gLastSubmitted;
static int gSubmitCount, gWriteCount, gMkdirCount;

enum { MAX_SECTIONS = 64, MAX_GPU = 4096 };
static struct { char name[32]; int parent, depth; uint32_t us, calls; } gSections[MAX_SECTIONS];
static int gNumSections;
static struct { uint32_t frame; uint64_t done; } gGpu[MAX_GPU];
static int gGpuHead, gGpuTail;

// ---- test controls ----

void FakeSetOutDir(const char *dir)
{
	snprintf(gRoot, sizeof(gRoot), "%s/", dir);
	for (char *p = gRoot; *p; p++) if (*p == '\\') *p = '/';
}
void FakeSetNow(uint64_t us) { gNow = us; }
void FakeSetCaps(uint32_t caps) { gCaps = caps; }
BenchCounters *FakeCounters(void) { return &gCounters; }
int FakeAddSection(const char *name, int parent, int depth)
{
	int i = gNumSections++;
	snprintf(gSections[i].name, sizeof(gSections[i].name), "%s", name);
	gSections[i].parent = parent;
	gSections[i].depth = depth;
	return i;
}
void FakeClearSectionFrame(void)
{
	for (int i = 0; i < gNumSections; i++) gSections[i].us = gSections[i].calls = 0;
}
void FakeSetSectionFrame(int i, uint32_t us, uint32_t calls) { gSections[i].us = us; gSections[i].calls = calls; }
void FakePushGpuDone(uint32_t frame, uint64_t doneUs)
{
	gGpu[gGpuTail % MAX_GPU].frame = frame;
	gGpu[gGpuTail % MAX_GPU].done = doneUs;
	gGpuTail++;
}
int FakePendingGpu(void) { return gGpuTail - gGpuHead; }
void FakeSetGpuSyncWait(uint32_t us) { gSyncWait = us; }
uint32_t FakeLastSubmitted(void) { return gLastSubmitted; }
int FakeSubmitCount(void) { return gSubmitCount; }
int FakeWriteCount(void) { return gWriteCount; }
int FakeMkdirCount(void) { return gMkdirCount; }

// ---- BenchPlatform ----

namespace BenchPlatform {

void Init(void) {}
uint32_t Caps(void) { return gCaps; }
uint64_t NowUs(void) { return gNow; }
void Tick(void) {}

const char *ResultsRoot(void) { return gRoot; }
void Timestamp(char *buf, int size) { snprintf(buf, size, "2026-09-30_14-05-00"); }

bool MakeDir(const char *path)
{
	gMkdirCount++;
#ifdef _WIN32
	_mkdir(path);
	struct _stat st;
	return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR);
#else
	mkdir(path, 0777);
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool WriteFile(const char *path, const void *data, uint32_t size)
{
	gWriteCount++;
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok;
}

int ReadSettingsFile(char *buf, int size) { if (size > 0) buf[0] = 0; return -1; }

void DeviceInfo(BenchDeviceInfo *out)
{
	memset(out, 0, sizeof(*out));
	snprintf(out->platform, sizeof(out->platform), "Host test");
	snprintf(out->model, sizeof(out->model), "fake-cpu");
	snprintf(out->firmware, sizeof(out->firmware), "0.00");
	snprintf(out->build, sizeof(out->build), "test-build-1");
	out->cpuMHz = 444; out->busMHz = 222; out->gpuMHz = 222; out->xbarMHz = 166;
	snprintf(out->gpu, sizeof(out->gpu), "FakeGPU");
	out->screenW = 960; out->screenH = 544;
	snprintf(out->notes, sizeof(out->notes), "fake \"platform\"\tnotes");
}

uint32_t RawButtons(void) { return 0; }

void ReadCounters(BenchCounters *out) { *out = gCounters; }

void ReadMemory(BenchMemory *out)
{
	memset(out, 0, sizeof(*out));
	out->heapUsed = 64u << 20; out->heapTotal = 256u << 20;
	out->gpuRamFree = 30u << 20; out->gpuRamTotal = 67u << 20;
	out->cdramFree = 12u << 20; out->cdramTotal = 96u << 20;
	out->phycontFree = 26u << 20; out->phycontTotal = 26u << 20;
}

// main runs 90 % of the time, CdStream 5 %
void ReadThreadTimes(BenchThreadTimes *out)
{
	memset(out, 0, sizeof(*out));
	out->count = 2;
	snprintf(out->name[0], sizeof(out->name[0]), "main");
	out->runUs[0] = gNow / 10 * 9;
	snprintf(out->name[1], sizeof(out->name[1]), "CdStream");
	out->runUs[1] = gNow / 20;
}

// core loads 90 / 50 / 10 / 0 %
void ReadCpuLoad(BenchCpuLoad *out)
{
	memset(out, 0, sizeof(*out));
	out->cores = 4;
	out->idleUs[0] = gNow / 10;
	out->idleUs[1] = gNow / 2;
	out->idleUs[2] = gNow / 10 * 9;
	out->idleUs[3] = gNow;
	out->wallUs = gNow;
}

void FrameSubmitted(uint32_t frame) { gLastSubmitted = frame; gSubmitCount++; }

// completions in push order, once the clock reached them
bool PollGpuDone(uint32_t *frame, uint64_t *doneUs)
{
	if (gGpuHead == gGpuTail || gGpu[gGpuHead % MAX_GPU].done > gNow) return false;
	*frame = gGpu[gGpuHead % MAX_GPU].frame;
	*doneUs = gGpu[gGpuHead % MAX_GPU].done;
	gGpuHead++;
	return true;
}

void SetGpuSync(bool on) { (void)on; }
uint32_t LastGpuSyncWaitUs(void) { return gSyncWait; }

bool SetViewportScale(float scale) { (void)scale; return false; }
bool SetDrawMode(int mode) { (void)mode; return false; }
bool SetFlatTextures(bool on) { (void)on; return false; }
void Begin3D(void) {}
void End3D(void) {}

int SectionCount(void) { return gNumSections; }
const char *SectionName(int i) { return i >= 0 && i < gNumSections ? gSections[i].name : NULL; }
int SectionParent(int i) { return i >= 0 && i < gNumSections ? gSections[i].parent : -1; }
int SectionDepth(int i) { return i >= 0 && i < gNumSections ? gSections[i].depth : 0; }
uint32_t SectionFrameUs(int i) { return i >= 0 && i < gNumSections ? gSections[i].us : 0; }
uint32_t SectionFrameCalls(int i) { return i >= 0 && i < gNumSections ? gSections[i].calls : 0; }

void SetPeriodicReports(bool on) { (void)on; }
void LogWindowBegin(void) {}
void LogWindowEnd(const char *scene) { (void)scene; }
void FlushLog(void) {}

} // namespace BenchPlatform
