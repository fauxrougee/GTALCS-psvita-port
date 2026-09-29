#pragma once

#ifdef PSP2

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

#endif
