#pragma once

#ifdef PSP2

#include <stdint.h>

// Measurement support documented in docs/DEVELOPMENT.md:
// gameplay frame statistics in log.txt, split by phase. The software limiter
// is disabled in this build, including with an old INI or menu preference.
// Report settings come from [VitaPerf] in reLCS.ini, read once at startup.
//
//   [VitaPerf]
//   FrameLimit        legacy key ignored; -1 internally means uncapped
//   ReportSeconds=5   report window, 1..60 s
//   GLCounters=1      count draw/state/upload GL calls (0: direct vitaGL entry
//                     points, to measure the counting overhead)

struct VitaPerfSettings {
	int frameLimit;
	int reportSeconds;
	bool glCounters;
};
const VitaPerfSettings &VitaPerfGetSettings(void);

// VitaInit, once the log is open: settings, build id, main thread, profiler cost.
void VitaPerfInit(void);
// Right after vitaGL's initialisation: presentation callback.
void VitaPerfGLReady(void);

// Microseconds since the process started (sceKernelGetProcessTimeWide).
uint64_t VitaPerfNow(void);

// Main loop, GS_PLAYING_GAME (glfw.cpp). FrameDue replaces the stable limiter
// test: always ready in this build; vsync is handled by the display queue.
bool VitaPerfFrameDue(bool limiter, bool vsync, float elapsedMs, int maxFPS);
// Around rsIDLE. Only a frame with exactly one swap and no menu is a gameplay
// frame; loading screens inside Idle, menus and frames without a swap are
// counted apart and interrupt the frame interval series.
void VitaPerfIdleBegin(bool menu);
void VitaPerfIdleEnd(uint32_t gameTimeMs, bool paused);

// glfwSwapBuffers and glfwSwapInterval (glfw_vita.cpp).
void VitaPerfSwapBegin(void);
void VitaPerfSwapEnd(void);
// vsync interval for glfwSwapInterval(interval): 0 off, 1 on, never 2.
int VitaPerfSwapInterval(int interval);

// Called from the thread itself: its run time goes in the reports (max 8).
void VitaPerfRegisterThread(const char *name);

// Storage (CdStreamPosix.cpp): reads on the stream thread, main thread waits
// in CdStreamSync, request queue depth after each request.
void VitaPerfDiskRead(uint32_t bytes, uint64_t us);
void VitaPerfDiskSync(uint64_t us);
void VitaPerfDiskQueued(int depth);
// CStreaming::Update: pending requests and memory used against the budget.
void VitaPerfStreaming(int requested, uint32_t memoryUsed, uint32_t memoryAvailable);

#ifdef RELCS_BENCHMARK
// reLCS Benchmark (vita_bench.cpp). Periodic windows, heartbeats and phase
// change reports are on by default; off, a window only ends with
// VitaPerfSceneWindowEnd and memory is not sampled during frames.
void VitaPerfSetPeriodicReports(bool on);
// Drops the window in progress; the next one starts at the next gameplay frame.
void VitaPerfSceneWindowBegin(void);
// Writes the window now (partial=1), " scene=<scene>" ending its window= line.
void VitaPerfSceneWindowEnd(const char *scene);
// Registered threads: name and run time in microseconds (false if not ready).
int VitaPerfThreadCount(void);
bool VitaPerfThreadRunTime(int i, const char **name, uint64_t *runUs);
// Storage and allocation totals since startup, never reset by the windows.
// Main thread only.
struct VitaPerfTotals {
	uint64_t diskReads, diskBytes, diskUs, syncWaits, syncUs;
	uint64_t allocCalls, freeCalls, allocBytes;
};
void VitaPerfGetTotals(VitaPerfTotals *out);
#endif

#endif
