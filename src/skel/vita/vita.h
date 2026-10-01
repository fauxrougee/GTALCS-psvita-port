#pragma once

#ifdef PSP2

#ifndef VITA_DATA_DIR
#define VITA_DATA_DIR "ux0:data/reLCS/"
#endif

// Called first thing in main(): clocks, working directory (VITA_DATA_DIR), log file.
void VitaInit(void);
void VitaFlushLog(void);

// Free memory left in the newlib heap, in bytes.
unsigned int VitaGetFreeMemory(void);

struct VitaPerformanceStats {
	float fps;
	float cpu[3]; // System load on application cores; -1 when unavailable.
	unsigned int heapUsed, heapTotal;
	unsigned int graphicsUsed, graphicsTotal; // All vitaGL-owned pools.
};
void VitaRecordPresentedFrame(void);
const VitaPerformanceStats &VitaGetPerformanceStats(void);

// System language as a POSIX-like locale name ("fr_FR", "en_US"...).
const char *VitaGetSystemLocale(void);

// Frame profiler: named sections (fed by the game's timebars and VPROF_*) and
// GL calls (gl_vita.cpp), averaged over the gameplay frames of each report
// window (vita_perf.cpp) and written to log.txt.
void VitaProfBegin(const char *name);
void VitaProfEnd(const char *name);
// End of a frame: keep its section times (gameplay) or drop them.
void VitaProfCommitFrame(void);
void VitaProfDiscardFrame(void);
// Cost of one begin/end pair in microseconds, measured once at startup.
double VitaProfCalibrate(void);
void VitaProfReportSections(int frames);
// Sections of the frame in progress (read before commit/discard). Indices stay
// valid for the whole session: the table only grows.
int VitaProfSectionCount(void);
const char *VitaProfSectionName(int i);
int VitaProfSectionParent(int i);	// -1 at top level
int VitaProfSectionDepth(int i);
unsigned int VitaProfSectionFrameUs(int i);
unsigned int VitaProfSectionFrameCalls(int i);

#ifdef RELCS_BENCHMARK
// VitaRecordPresentedFrame's heap/vitaGL pool sampling every 0.5 s (mallinfo
// gets slower as the heap fragments). On by default; the benchmark turns it off.
void VitaSetFrameSampling(bool on);
// Refreshes the heap and pool figures of VitaGetPerformanceStats now.
void VitaSampleMemoryNow(void);
#endif

#endif
