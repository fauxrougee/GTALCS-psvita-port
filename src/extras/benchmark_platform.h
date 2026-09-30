#pragma once

// Platform layer of the benchmark (see benchmark.h).
//   Vita:    src/skel/vita/vita_bench.cpp (vitaGL/GXM, sceKernel, VitaPerf)
//   Windows: src/extras/benchmark_platform_pc.cpp (QueryPerformanceCounter)
// Everything a platform cannot do reports "unsupported" through Caps(); the
// director then skips or marks the matching tests instead of guessing.

#ifdef RELCS_BENCHMARK

#include <stdint.h>

// Capabilities
enum {
	BENCH_CAP_GPU_FENCE   = 1 << 0,  // GPU completion time of each frame (non intrusive)
	BENCH_CAP_GPU_SYNC    = 1 << 1,  // wait for the GPU after each swap and time the wait
	BENCH_CAP_VIEWPORT    = 1 << 2,  // shrink the 3D viewport (fill-rate tests)
	BENCH_CAP_DRAW_MODE   = 1 << 3,  // tiny / null draw calls (CPU vs GPU split)
	BENCH_CAP_FLAT_TEX    = 1 << 4,  // bind a 1x1 white texture instead (texture cost)
	BENCH_CAP_SECTIONS    = 1 << 5,  // per-frame profiler sections (VPERF/VPROF/timebars)
	BENCH_CAP_GL_COUNTERS = 1 << 6,  // GL call counters
	BENCH_CAP_DISK        = 1 << 7,  // stream thread disk reads
	BENCH_CAP_THREADS     = 1 << 8,  // per-thread CPU time
	BENCH_CAP_CPU_LOAD    = 1 << 9,  // per-core load
	BENCH_CAP_ALLOC       = 1 << 10, // malloc/free counters
};

// Raw buttons, read directly from the hardware (the game's pad is locked).
enum {
	BENCH_BTN_CROSS    = 1 << 0,  // PC: Enter
	BENCH_BTN_CIRCLE   = 1 << 1,  // PC: Backspace
	BENCH_BTN_START    = 1 << 2,  // PC: Escape
	BENCH_BTN_SELECT   = 1 << 3,  // PC: Tab
	BENCH_BTN_TRIANGLE = 1 << 4,
	BENCH_BTN_UP       = 1 << 5,
	BENCH_BTN_DOWN     = 1 << 6,
};

// Draw modes (BENCH_CAP_DRAW_MODE), between Bench::Begin3D and End3D
enum {
	BENCH_DRAW_NORMAL = 0,
	BENCH_DRAW_TINY   = 1,  // every draw call submits at most 3 indices: all CPU and
	                        // driver work is kept, GPU geometry/fragment work ~0
	BENCH_DRAW_NULL   = 2,  // draw calls return before reaching the driver
};

// Cumulative counters (monotonic since Init, never reset); the metrics code
// takes deltas per frame. Unsupported fields stay 0.
struct BenchCounters {
	uint64_t draws, indices;          // glDrawElements + glDrawArrays, indices/vertices
	uint64_t uniforms, texBinds, bufBinds, programs, stateCalls;
	uint64_t vertexUploadBytes, indexUploadBytes, textureUploadBytes;
	uint64_t textureUploadUs;         // time in glTexImage2D & co
	uint64_t shaderCompiles;          // glCompileShader + glLinkProgram calls
	uint64_t diskReads, diskBytes, diskReadUs, diskSyncWaits, diskSyncUs;
	uint64_t allocCalls, freeCalls, allocBytes;
};

struct BenchMemory {
	uint32_t heapUsed, heapTotal;           // bytes; 0 = unknown
	uint32_t gpuRamFree, gpuRamTotal;       // vitaGL RAM pool
	uint32_t cdramFree, cdramTotal;         // vitaGL VRAM (CDRAM) pool
	uint32_t phycontFree, phycontTotal;
};

struct BenchDeviceInfo {
	char platform[16];      // "PS Vita", "PS TV", "Windows"
	char model[32];         // e.g. "PCH-1000 (0x10000)" or CPU name
	char firmware[16];
	char build[64];         // VITA_BUILD_ID or "pc-<date>"
	int cpuMHz, busMHz, gpuMHz, xbarMHz;  // 0 = unknown
	char gpu[32];           // "SGX543MP4+ (vitaGL)" or "Direct3D 9"
	int screenW, screenH;
	char notes[128];        // anything the platform wants to report (e.g. CPU load probe status)
};

#define BENCH_MAX_THREADS 12
struct BenchThreadTimes {
	int count;
	char name[BENCH_MAX_THREADS][24];
	uint64_t runUs[BENCH_MAX_THREADS];   // cumulative CPU time
};

struct BenchCpuLoad {
	int cores;               // 0 = unsupported
	uint64_t idleUs[4];      // cumulative idle time per core
	uint64_t wallUs;         // timestamp of the sample
};

namespace BenchPlatform {

void Init(void);             // once, from Bench::OnSettingsLoaded
uint32_t Caps(void);
uint64_t NowUs(void);        // monotonic microseconds
void Tick(void);             // every frame: keep the console awake, etc.

// Results: "<root><YYYY-MM-DD_HH-MM-SS>/". Root ends with '/'.
const char *ResultsRoot(void);        // "ux0:data/reLCS/benchmark/" or "benchmark/"
void Timestamp(char *buf, int size);  // local time "2026-09-30_14-05-00"
bool MakeDir(const char *path);       // one level; true if it exists afterwards
// Writes a whole file in one go (binary, truncating). Returns false on error.
bool WriteFile(const char *path, const void *data, uint32_t size);
// Optional settings file "benchmark.ini" (Vita: ux0:data/reLCS/benchmark.ini,
// PC: assets/benchmark.ini). Returns its size, or -1. buf is 0-terminated.
int ReadSettingsFile(char *buf, int size);

void DeviceInfo(BenchDeviceInfo *out);
uint32_t RawButtons(void);

void ReadCounters(BenchCounters *out);
void ReadMemory(BenchMemory *out);          // may take a few ms: call between tests only
void ReadThreadTimes(BenchThreadTimes *out);
void ReadCpuLoad(BenchCpuLoad *out);

// GPU timing.
// FrameSubmitted: right after the swap of benchmark frame `frame`.
// PollGpuDone: drains completed frames; returns false when none is left.
// doneUs is on the NowUs() clock.
void FrameSubmitted(uint32_t frame);
bool PollGpuDone(uint32_t *frame, uint64_t *doneUs);
// GPU sync: after each swap, block until the GPU is idle; the wait is
// returned by LastGpuSyncWaitUs() (valid right after SwapEnd).
void SetGpuSync(bool on);
uint32_t LastGpuSyncWaitUs(void);

// Render experiments, active only between Begin3D() and End3D() calls below.
// Return false when unsupported.
bool SetViewportScale(float scale);   // 1 = off
bool SetDrawMode(int mode);           // BENCH_DRAW_*
bool SetFlatTextures(bool on);
void Begin3D(void);
void End3D(void);

// Profiler sections of the frame in progress (BENCH_CAP_SECTIONS): call
// before the platform commits the frame (Bench::FrameEnd). Indices are stable
// for the whole run; the list only grows.
int SectionCount(void);
const char *SectionName(int i);
int SectionParent(int i);    // -1 for top level
int SectionDepth(int i);
uint32_t SectionFrameUs(int i);
uint32_t SectionFrameCalls(int i);

// Platform log windows (Vita: "[PERF] window=... scene=<id>" in the log, as
// parsed by tools/vita/perf-report.py). Periodic reports are disabled while
// the benchmark runs; one window per measured test instead.
void SetPeriodicReports(bool on);
void LogWindowBegin(void);
void LogWindowEnd(const char *scene);
void FlushLog(void);

} // namespace BenchPlatform

#endif
