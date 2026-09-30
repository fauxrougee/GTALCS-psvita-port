#pragma once

// Benchmark measurements: per-frame records, per-test results, analysis and
// the result files (see benchmark.h). Pure C++ over BenchPlatform, no engine
// headers, so it can be unit-tested on a host with a fake platform.
//
// Result folder <root>/<timestamp>/ contains:
//   results.json  everything below, schema "relcs-bench/1" (documented in
//                 docs/BENCHMARK.md and read by tools/vita/bench-report.py)
//   frames.csv    one line per measured frame (BENCH_CSV_HEADER)
//   summary.txt   French text summary: scores, per-test table, what to optimise
//   log.txt       (Vita) engine log with one [PERF] window per test

#ifdef RELCS_BENCHMARK

#include <stdint.h>
#include "benchmark_platform.h"

// Top-level profiler groups kept per frame (sections are also aggregated
// per test in full). A section name maps to a group by exact name at depth 0.
enum BenchGroup {
	BG_SIM,        // "CGame::Process"
	BG_AUDIO,      // "DMAudio.Service"
	BG_RENDERLIST, // "CnstrRenderList", "PreRender"
	BG_ENVMAP,     // "EnvMapBeforeFrame", "EnvMapRender"
	BG_SCENE,      // "StartOfFrame", "RenderScene"
	BG_FX,         // "RenderEffects", "RenderMotionBlur"
	BG_2D,         // "Render2dStuff", "RenderMenus", "DoFade", "Render2dStuff-Fade"
	BG_EOF,        // "EndOfFrame" (includes the swap)
	BG_COUNT
};
extern const char *const gBenchGroupNames[BG_COUNT];   // "sim","audio",...

enum {
	BF_EXTRA_SWAP = 1 << 0,  // more than one swap in the frame (loading screen inside)
	BF_NO_SWAP    = 1 << 1,
	BF_GPU_SYNC   = 1 << 2,  // gpu-sync mode frame
	BF_PAUSED     = 1 << 3,  // simulation frozen (render-only frame)
	BF_HITCH      = 1 << 4,  // frame > max(100 ms, 3 x test median), set by analysis
};

struct BenchFrame {
	uint16_t test;            // index in BenchRun::tests
	uint16_t flags;           // BF_*
	uint32_t index;           // frame number inside the test (0..)
	uint32_t tUs;             // FrameBegin time since the test's measurement start
	uint32_t frameUs;         // this FrameBegin -> next FrameBegin
	uint32_t workUs;          // FrameBegin -> first SwapBegin (CPU main thread)
	uint32_t swapUs;          // time inside swaps
	uint32_t gpuUs;           // GPU time of the frame (fence), 0 = unknown
	uint32_t gpuSyncUs;       // gpu-sync wait, 0 when not in that mode
	uint32_t groupUs[BG_COUNT];
	uint32_t draws, indices, uniforms, texBinds;
	uint32_t uploadBytes;     // vertex + index + texture uploads
	uint32_t diskBytes;
	uint16_t peds, vehicles, objects, streamRequests;
	float camX, camY, camZ;
};

#define BENCH_CSV_HEADER "test,frame,t_ms,frame_ms,work_ms,swap_ms,gpu_ms,gpu_sync_ms," \
	"sim_ms,audio_ms,renderlist_ms,envmap_ms,scene_ms,fx_ms,2d_ms,eof_ms," \
	"draws,indices,uniforms,tex_binds,upload_kib,disk_kib,peds,vehicles,objects,stream_req," \
	"cam_x,cam_y,cam_z,flags"

// A section aggregated over the measured frames of one test.
#define BENCH_MAX_SECTIONS 128
struct BenchSectionStat {
	int16_t platformIndex;    // BenchPlatform section index
	int16_t parent;           // index in this array, -1 = top level
	uint8_t depth;
	char name[32];
	double totalUs;           // inclusive
	double selfUs;            // inclusive minus direct children (filled by analysis)
	uint32_t maxFrameUs;
	uint32_t calls;
	uint32_t framesPresent;
};

struct BenchStats {           // distribution of one per-frame value, in ms
	double mean, median, p90, p95, p99, min, max;
	double low1Fps;           // "1% low" fps: 1000 / mean of the slowest 1% of frames
};

// Test kinds (what the director did, for the report)
enum BenchTestKind {
	BTK_FLYTHROUGH,   // scripted camera path
	BTK_STATIC,       // fixed camera (crowd, isolation variants)
	BTK_FOLLOW,       // camera follows an AI driven car
	BTK_STRESS,       // explosions, fires, riot
	BTK_STREAMING,    // fast travel across islands
	BTK_TELEPORT,     // LoadScene timings
};

#define BENCH_MAX_TESTS 128
struct BenchTestResult {
	// identity
	char id[32];              // stable id, e.g. "g1_portland_el", "iso_a_no_shadows"
	char group[16];           // "graphics", "cpu", "streaming", "isolation", "boot"
	char name[64];            // French display name
	char variant[32];         // isolation variant id ("baseline", "no_shadows"...), "" otherwise
	char viewpoint[32];       // isolation viewpoint id, "" otherwise
	int kind;                 // BenchTestKind
	bool skipped;             // capability missing / aborted before measuring
	char skipReason[64];
	bool interrupted;         // suspend or > 2 s frame during the test (PS button...)
	// scene settings
	int hour, minute, weather;
	float pedDensity, carDensity;
	bool fixedStep, paused;
	// measurement
	uint32_t firstFrame, frameCount;   // range in BenchRun::frames
	double seconds;                    // real time measured
	double fps;                        // frames / seconds
	BenchStats frameMs, workMs, swapMs, gpuMs, gpuSyncMs;
	uint32_t gpuSamples;               // frames with a GPU time
	double cpuBoundPct, gpuBoundPct;   // among GPU samples: work/GPU dominates; -1 = unknown
	double groupMs[BG_COUNT];          // mean per frame
	uint32_t hitches;                  // BF_HITCH frames
	double worstHitchMs;
	char worstHitchGroup[16];          // group with the largest time in the worst hitch
	// per frame means
	double draws, indices, uniforms, texBinds, uploadKiB, diskKiB;
	double peds, vehicles, objects;
	// totals over the test (counter deltas)
	BenchCounters counters;
	BenchThreadTimes threadsStart, threadsEnd;
	BenchCpuLoad cpuStart, cpuEnd;
	BenchMemory memEnd;
	// teleport / load timings (BTK_TELEPORT, BTK_STREAMING), ms, -1 = n/a
	double loadMs, settleMs;
	// sections
	int numSections;
	BenchSectionStat sections[BENCH_MAX_SECTIONS];
};

// Isolation analysis: variant vs the viewpoint's baseline
struct BenchDelta {
	char viewpoint[32];
	char variant[32];
	double baselineMs, variantMs;      // mean frame time
	double savedMs;                    // baseline - variant (> 0: the feature costs this much)
	double savedPct;
	double baselineGpuMs, variantGpuMs;   // -1 = unknown
	double baselineWorkMs, variantWorkMs;
};

// Ranked optimisation targets, computed from the gameplay-like tests
struct BenchTarget {
	char what[48];      // section / feature name
	char kind[16];      // "section", "feature", "gpu", "streaming", "cpu"
	double ms;          // mean cost per frame (or hitch ms for streaming)
	double pct;         // share of the frame
	char advice[160];   // French one-liner
};

#define BENCH_MAX_DELTAS 128
#define BENCH_MAX_TARGETS 16

struct BenchScores {
	double graphics;    // 100 x geometric mean fps of the graphics group
	double cpu;         // 100 x geometric mean fps of the cpu group
	double overall;     // 100 x geometric mean over graphics + cpu + streaming (excl. isolation)
	bool valid;
};

struct BenchRun {
	char folder[128];                 // "<root><timestamp>/"
	char timestamp[32];
	BenchDeviceInfo device;
	uint32_t caps;
	char settings[512];               // effective settings, "key=value;..." for the report
	// boot timings (ms since process start, -1 = unknown)
	double bootEngineMs, bootGameMs, bootReadyMs;
	bool aborted;
	int numTests;
	BenchTestResult *tests;           // BENCH_MAX_TESTS, heap
	uint32_t numFrames, maxFrames;
	BenchFrame *frames;               // heap
	int numDeltas;
	BenchDelta deltas[BENCH_MAX_DELTAS];
	int numTargets;
	BenchTarget targets[BENCH_MAX_TARGETS];
	BenchScores scores;
};

namespace BenchMetrics {

// Allocates the frame buffer (maxFrames records) and the test table.
bool Init(BenchRun *run, uint32_t maxFrames);
BenchRun *Run(void);

// ---- Test lifecycle (director) ----
// BeginTest returns the new test (identity/settings filled by the caller
// afterwards); samples start counters, threads, CPU load.
BenchTestResult *BeginTest(const char *id, const char *group, const char *name, int kind);
// Measurement window: frames between StartMeasuring and StopMeasuring are
// recorded into the current test. Frames outside (title cards, warm-up) are
// timed but not recorded.
void StartMeasuring(void);
void StopMeasuring(void);
bool Measuring(void);
// Ends the test: computes its statistics (EndTest may take a few ms).
void EndTest(void);
void SkipTest(BenchTestResult *t, const char *reason);

// ---- Frame hooks (called by Bench::FrameBegin/FrameEnd/SwapBegin/SwapEnd) ----
void FrameBegin(uint64_t nowUs);
void SwapBegin(uint64_t nowUs);
void SwapEnd(uint64_t nowUs);
// Per-frame context from the director, set before FrameEnd.
void SetFrameContext(float camX, float camY, float camZ, int peds, int vehicles, int objects,
                     int streamRequests, bool paused, bool gpuSync);
void FrameEnd(uint64_t nowUs);

// ---- Analysis and output ----
// Isolation deltas, optimisation targets, scores. Call once after the last test.
void Analyse(void);
// Writes results.json, frames.csv, summary.txt into run->folder (created if
// needed). Returns false if any file failed. Slow: call off-measurement.
bool WriteResults(void);
// Incremental save after each test (results.json only, "partial": true), so an
// aborted or crashed run still leaves data. Cheap enough between tests.
bool WritePartial(void);

// Statistics helper (exposed for tests): values in ms, n > 0; scratch has room for n.
void ComputeStats(const double *values, int n, double *scratch, BenchStats *out);

} // namespace BenchMetrics

#endif
