// Host test of src/extras/benchmark_metrics.cpp over bench_fake_platform.cpp.
// Drives a synthetic benchmark run with known frame times, then writes the
// result files into argv[1]/2026-09-30_14-05-00/ for check-bench-metrics.py.
// Also copies the partial results.json written after g2 to argv[1]/partial.json.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "benchmark_metrics.h"

void FakeSetOutDir(const char *dir);
void FakeSetNow(uint64_t us);
void FakeSetCaps(uint32_t caps);
BenchCounters *FakeCounters(void);
int FakeAddSection(const char *name, int parent, int depth);
void FakeClearSectionFrame(void);
void FakeSetSectionFrame(int i, uint32_t us, uint32_t calls);
void FakePushGpuDone(uint32_t frame, uint64_t doneUs);
int FakePendingGpu(void);
void FakeSetGpuSyncWait(uint32_t us);
uint32_t FakeLastSubmitted(void);
int FakeSubmitCount(void);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static bool Near(double a, double b, double eps = 1e-6) { return fabs(a - b) <= eps; }

static uint64_t T = 1000000;
static uint64_t gLastDone;
static bool gHaveDone;
enum { S_CG, S_WP, S_WC, S_DMA, S_RS, S_SW1, S_EOF, S_SU, S_LOADALL };

enum { F_NO_SWAP = 1, F_EXTRA_SWAP = 2, F_HITCH_SECTIONS = 4 };
struct FrameSpec {
	uint32_t work, swap, tail;
	int flags;
	uint32_t gpuCost;       // > 0: push a GPU completion for this frame
	uint32_t syncWait;      // > 0: gpu-sync frame
	bool paused;
};

static void Advance(uint64_t us) { T += us; FakeSetNow(T); }

static void DoFrame(const FrameSpec &f)
{
	FakeSetNow(T);
	BenchMetrics::FrameBegin(T);
	BenchCounters *c = FakeCounters();
	c->draws += 1000; c->indices += 100000; c->uniforms += 2000; c->texBinds += 800;
	c->bufBinds += 500; c->programs += 10; c->stateCalls += 1300;
	c->vertexUploadBytes += 40960; c->textureUploadBytes += 10240;
	c->diskReads += 1; c->diskBytes += 20480; c->diskReadUs += 1500;
	c->allocCalls += 10; c->freeCalls += 9; c->allocBytes += 4096;

	FakeClearSectionFrame();
	if (f.flags & F_HITCH_SECTIONS) {
		FakeSetSectionFrame(S_CG, 465000, 1);
		FakeSetSectionFrame(S_SU, 460000, 1);
		FakeSetSectionFrame(S_LOADALL, 450000, 3);
	} else
		FakeSetSectionFrame(S_CG, 5000, 1);
	FakeSetSectionFrame(S_WP, 3000, 1);
	FakeSetSectionFrame(S_WC, 2000, 40);
	FakeSetSectionFrame(S_DMA, 1000, 1);
	FakeSetSectionFrame(S_RS, 4000, 1);
	FakeSetSectionFrame(S_SW1, 3000, 1);
	FakeSetSectionFrame(S_EOF, (f.flags & F_EXTRA_SWAP) ? 2 * f.swap : f.swap, 1);
	FakeSetGpuSyncWait(f.syncWait);

	uint64_t swapBegin = 0;
	if (f.flags & F_NO_SWAP)
		Advance(f.work);
	else if (f.flags & F_EXTRA_SWAP) {	// loading screen swap inside the frame
		Advance(f.work / 2);
		swapBegin = T;
		BenchMetrics::SwapBegin(T); Advance(f.swap); BenchMetrics::SwapEnd(T);
		Advance(f.work - f.work / 2);
		BenchMetrics::SwapBegin(T); Advance(f.swap); BenchMetrics::SwapEnd(T);
	} else {
		Advance(f.work);
		swapBegin = T;
		BenchMetrics::SwapBegin(T); Advance(f.swap); BenchMetrics::SwapEnd(T);
	}
	if (f.gpuCost && swapBegin) {
		uint64_t start = gHaveDone && gLastDone > swapBegin ? gLastDone : swapBegin;
		gLastDone = start + f.gpuCost;
		gHaveDone = true;
		FakePushGpuDone(FakeLastSubmitted(), gLastDone);
	}
	Advance(f.syncWait);	// the gpu-sync wait happens inside FrameSubmitted
	BenchMetrics::SetFrameContext(100.0f, -200.5f, 15.25f, 30, 12, 40, 3, f.paused, f.syncWait > 0);
	BenchMetrics::FrameEnd(T);
	Advance(f.tail);
}

static FrameSpec Spec(uint32_t work, uint32_t swap)
{
	FrameSpec f;
	memset(&f, 0, sizeof(f));
	f.work = work;
	f.swap = swap;
	return f;
}

static BenchTestResult *Begin(const char *id, const char *group, const char *name, int kind)
{
	BenchTestResult *t = BenchMetrics::BeginTest(id, group, name, kind);
	t->hour = 13; t->minute = 0; t->weather = 0;
	t->pedDensity = 1.0f; t->carDensity = 1.0f;
	t->fixedStep = true;
	return t;
}

static void Frames(int n, const FrameSpec &f) { for (int i = 0; i < n; i++) DoFrame(f); }

static void StatsUnitTests(void)
{
	double v[250], s[250];
	BenchStats st;
	// 1..100 in scrambled order: nearest-rank percentiles
	for (int i = 0; i < 100; i++) v[i] = (double)((i * 37) % 100 + 1);
	BenchMetrics::ComputeStats(v, 100, s, &st);
	CHECK(Near(st.mean, 50.5) && Near(st.median, 50) && Near(st.p90, 90) && Near(st.p95, 95) && Near(st.p99, 99));
	CHECK(Near(st.min, 1) && Near(st.max, 100) && Near(st.low1Fps, 10));
	for (int i = 0; i < 250; i++) v[i] = i + 1;
	BenchMetrics::ComputeStats(v, 250, s, &st);
	CHECK(Near(st.low1Fps, 1000.0 / 249.5) && Near(st.median, 125) && Near(st.p99, 248));
	v[0] = 16.5;
	BenchMetrics::ComputeStats(v, 1, s, &st);
	CHECK(Near(st.median, 16.5) && Near(st.p99, 16.5) && Near(st.min, 16.5) && Near(st.low1Fps, 1000 / 16.5));
	BenchMetrics::ComputeStats(v, 0, s, &st);
	CHECK(st.mean == 0 && st.max == 0);
}

static void Isolation(const char *vp, bool setFields, const char *variant, uint32_t work, uint32_t swap,
                      bool paused, uint32_t sync = 0, const char *skip = NULL)
{
	char id[64];
	snprintf(id, sizeof(id), "iso_%s_%s", vp, variant);
	BenchTestResult *t = Begin(id, "isolation", variant, BTK_STATIC);
	t->hour = 12;
	t->paused = paused;
	if (setFields) {
		snprintf(t->viewpoint, sizeof(t->viewpoint), "%s", vp);
		snprintf(t->variant, sizeof(t->variant), "%s", variant);
	}
	if (skip) {
		BenchMetrics::SkipTest(t, skip);
		BenchMetrics::EndTest();	// no-op after SkipTest
		return;
	}
	FrameSpec f = Spec(work, swap);
	f.paused = paused;
	f.syncWait = sync;
	Frames(2, f);	// settle, not recorded
	BenchMetrics::StartMeasuring();
	Frames(20, f);
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();
}

static bool CopyFile(const char *from, const char *to)
{
	FILE *a = fopen(from, "rb");
	if (!a) return false;
	FILE *b = fopen(to, "wb");
	if (!b) { fclose(a); return false; }
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
	fclose(a);
	return fclose(b) == 0;
}

int main(int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : getenv("BENCH_TEST_OUT");
	CHECK(out != NULL);
	FakeSetOutDir(out);
	FakeSetNow(T);
	StatsUnitTests();

	FakeSetCaps(BENCH_CAP_GPU_FENCE | BENCH_CAP_GPU_SYNC | BENCH_CAP_SECTIONS | BENCH_CAP_GL_COUNTERS |
	            BENCH_CAP_DISK | BENCH_CAP_THREADS | BENCH_CAP_CPU_LOAD | BENCH_CAP_ALLOC);
	CHECK(FakeAddSection("CGame::Process", -1, 0) == S_CG);
	CHECK(FakeAddSection("World::Process", S_CG, 1) == S_WP);
	CHECK(FakeAddSection("World.Control", S_WP, 2) == S_WC);
	CHECK(FakeAddSection("DMAudio.Service", -1, 0) == S_DMA);
	CHECK(FakeAddSection("RenderScene", -1, 0) == S_RS);
	CHECK(FakeAddSection("Scene.World1Opaque", S_RS, 1) == S_SW1);
	CHECK(FakeAddSection("EndOfFrame", -1, 0) == S_EOF);
	CHECK(FakeAddSection("Streaming::Update", S_CG, 1) == S_SU);
	CHECK(FakeAddSection("Streaming::LoadAll", S_SU, 2) == S_LOADALL);

	static BenchRun run;
	CHECK(BenchMetrics::Init(&run, 20000));
	CHECK(BenchMetrics::Run() == &run);
	snprintf(run.settings, sizeof(run.settings), "Mode=full;FixedStep=1;Seed=1234;Tests=");
	run.bootEngineMs = 1234.5; run.bootGameMs = 5678.25; run.bootReadyMs = 9012.0;
	CHECK(strcmp(run.timestamp, "2026-09-30_14-05-00") == 0);

	// frames before any test are timed, never recorded
	Frames(3, Spec(30000, 1000));
	CHECK(run.numFrames == 0);

	// g1: 11..20 ms frames, ten of each
	BenchTestResult *t = Begin("g1_portland_el", "graphics", "Portland - metro aerien", BTK_FLYTHROUGH);
	Frames(3, Spec(50000, 1000));
	BenchMetrics::StartMeasuring();
	CHECK(BenchMetrics::Measuring());
	for (int i = 0; i < 100; i++) DoFrame(Spec(10000 + (i % 10) * 1000, 1000));
	BenchMetrics::StopMeasuring();
	CHECK(!BenchMetrics::Measuring());
	BenchMetrics::EndTest();
	CHECK(t->frameCount == 100 && Near(t->fps, 100 / 1.55, 1e-9) && Near(t->frameMs.median, 15));
	CHECK(t->counters.draws == 100000 && t->gpuSamples == 0);
	CHECK(t->cpuBoundPct == -1 && t->gpuBoundPct == -1);

	// g2: GPU bound, 8 ms work + 12 ms swap, 18 ms of GPU per frame
	t = Begin("g2_staunton_towers", "graphics", "Staunton - gratte-ciel", BTK_FLYTHROUGH);
	FrameSpec g2 = Spec(8000, 12000);
	g2.gpuCost = 18000;
	Frames(5, g2);
	BenchMetrics::StartMeasuring();
	Frames(150, g2);
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();
	CHECK(t->frameCount == 150 && t->gpuSamples == 149 && Near(t->gpuMs.mean, 18));
	CHECK(Near(t->cpuBoundPct, 0) && Near(t->gpuBoundPct, 100));
	CHECK(FakePendingGpu() == 1);	// the last frame completes during the next test
	CHECK(BenchMetrics::WritePartial());
	{
		char a[600], b[600];
		snprintf(a, sizeof(a), "%s/2026-09-30_14-05-00/results.json", out);
		snprintf(b, sizeof(b), "%s/partial.json", out);
		CHECK(CopyFile(a, b));
	}

	// c1 x1: 25 ms frames, one without swap, one stray swap between frames
	t = Begin("c1_crowd_d1", "cpu", "Foule x1", BTK_STATIC);
	t->hour = 12;
	BenchMetrics::StartMeasuring();
	for (int i = 0; i < 50; i++) {
		FrameSpec f = Spec(24000, 1000);
		if (i == 10) { f = Spec(25000, 0); f.flags = F_NO_SWAP; }
		DoFrame(f);
		if (i == 20) {
			int submits = FakeSubmitCount();
			BenchMetrics::SwapBegin(T);
			BenchMetrics::SwapEnd(T);
			CHECK(FakeSubmitCount() == submits);
		}
	}
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();
	CHECK(t->frameCount == 50 && Near(t->fps, 40, 1e-9));
	// g2's last GPU completion arrived meanwhile: patched after its EndTest
	CHECK(FakePendingGpu() == 0 && run.tests[1].gpuSamples == 149);

	// c1 x3: interrupted by the user, excluded from the scores
	t = Begin("c1_crowd_d3", "cpu", "Foule x3", BTK_STATIC);
	t->pedDensity = t->carDensity = 3.0f;
	t->interrupted = true;
	BenchMetrics::StartMeasuring();
	Frames(20, Spec(49000, 1000));
	BenchMetrics::EndTest();	// ends the measurement itself

	// c2: AI driving, 30 ms frames
	t = Begin("c2_traffic_follow", "cpu", "Conduite IA", BTK_FOLLOW);
	BenchMetrics::StartMeasuring();
	Frames(50, Spec(28000, 2000));
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();

	// s1: 20 ms frames with one 500 ms island load (and a loading screen swap)
	t = Begin("s1_bridge_run", "streaming", "Streaming: pont Callahan", BTK_STREAMING);
	t->fixedStep = false;
	BenchMetrics::StartMeasuring();
	for (int i = 0; i < 60; i++) {
		FrameSpec f = Spec(19000, 1000);
		if (i == 30) { f = Spec(495000, 2500); f.flags = F_EXTRA_SWAP | F_HITCH_SECTIONS; }
		DoFrame(f);
	}
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();
	CHECK(t->hitches == 1 && Near(t->worstHitchMs, 500) && strcmp(t->worstHitchGroup, "sim") == 0);

	// teleports: LoadScene reads 5 MiB before the measured settle
	t = Begin("s2_tp_redlight", "streaming", "Teleportation: Red Light", BTK_TELEPORT);
	FakeCounters()->diskBytes += 5u << 20;
	FakeCounters()->diskReads += 50;
	t->loadMs = 850; t->settleMs = 1200;
	BenchMetrics::StartMeasuring();
	Frames(30, Spec(39000, 1000));
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();
	CHECK(t->counters.diskBytes == (5u << 20) + 30 * 20480u);
	t = Begin("s2_tp_bedford", "streaming", "Teleportation: Bedford Point", BTK_TELEPORT);
	t->loadMs = 1300; t->settleMs = 900;
	BenchMetrics::StartMeasuring();
	Frames(20, Spec(44000, 1000));
	BenchMetrics::StopMeasuring();
	BenchMetrics::EndTest();

	// isolation: a_chinatown with explicit fields, b_bedford parsed from the ids
	Isolation("a_chinatown", true, "baseline", 25000, 5000, true);
	Isolation("a_chinatown", true, "no_shadows", 23000, 5000, true);
	Isolation("a_chinatown", true, "no_peds", 22000, 5000, true);
	Isolation("a_chinatown", true, "no_water", 24800, 5000, true);
	Isolation("a_chinatown", true, "no_vehicles", 24000, 5000, true);
	Isolation("a_chinatown", true, "hud_on", 26500, 5000, true);
	Isolation("a_chinatown", true, "viewport_50", 15000, 5000, true);
	Isolation("a_chinatown", true, "draw_tiny", 0, 0, true, 0, "capacite absente (draw_mode)");
	Isolation("a_chinatown", true, "gpu_sync", 25000, 5000, true, 4000);
	Isolation("a_chinatown", true, "baseline_end", 25100, 5000, true);
	Isolation("a_chinatown", true, "sim_running", 30000, 5000, false);
	Isolation("a_chinatown", true, "no_audio", 28500, 5000, false);
	Isolation("b_bedford", false, "baseline", 30000, 10000, true);
	Isolation("b_bedford", false, "no_shadows", 27000, 10000, true);
	Isolation("b_bedford", false, "no_peds", 29000, 10000, true);
	Isolation("b_bedford", false, "lod_high", 31100, 10000, true);
	Isolation("b_bedford", false, "viewport_50", 0, 0, true, 0, "capacite absente (viewport)");
	Isolation("b_bedford", false, "sim_running", 34000, 10000, false);
	Isolation("b_bedford", false, "no_audio", 33000, 10000, false);

	BenchMetrics::Analyse();
	CHECK(run.numFrames == 820);
	CHECK(run.numDeltas == 15);
	CHECK(run.tests[1].gpuSamples == 150 && Near(run.tests[1].gpuMs.mean, 18));
	CHECK(run.scores.valid && run.numTargets == BENCH_MAX_TARGETS);	// 17 candidates
	for (int i = 0; i < run.numTargets; i++) {
		CHECK(run.targets[i].advice[0] != 0);
		for (const char *p = run.targets[i].advice; *p; p++) CHECK((unsigned char)*p < 0x80);
		if (i) CHECK(run.targets[i - 1].ms >= run.targets[i].ms);
	}
	// the gpu-sync frames carry the wait
	for (int i = 0; i < run.numTests; i++)
		if (strcmp(run.tests[i].id, "iso_a_chinatown_gpu_sync") == 0)
			CHECK(Near(run.tests[i].gpuSyncMs.mean, 4) && Near(run.tests[i].frameMs.mean, 34));
	CHECK(BenchMetrics::WriteResults());

	// second run: frame buffer overflow is counted, not written past
	static BenchRun small;
	snprintf(small.folder, sizeof(small.folder), "%s/overflow/deep/", out);
	CHECK(BenchMetrics::Init(&small, 5));
	Begin("g1_portland_el", "graphics", "Portland - metro aerien", BTK_FLYTHROUGH);
	BenchMetrics::StartMeasuring();
	Frames(10, Spec(10000, 1000));
	BenchMetrics::EndTest();
	CHECK(small.numFrames == 5 && small.tests[0].frameCount == 5);
	CHECK(!small.scores.valid);
	CHECK(BenchMetrics::WriteResults());

	// An unavailable Vita GPU fence must not turn display/GC waits into GPU
	// measurements or optimisation advice, even with a very long swap.
	static BenchRun unavailable;
	FakeSetCaps(0);
	snprintf(unavailable.folder, sizeof(unavailable.folder), "%s/unavailable/", out);
	CHECK(BenchMetrics::Init(&unavailable, 64));
	t = Begin("g1_portland_el", "graphics", "GPU unavailable", BTK_FLYTHROUGH);
	BenchMetrics::StartMeasuring();
	Frames(20, Spec(1000, 30000));
	BenchMetrics::EndTest();
	CHECK(t->frameCount == 20 && t->gpuSamples == 0);
	CHECK(t->cpuBoundPct == -1 && t->gpuBoundPct == -1);
	BenchMetrics::Analyse();
	for (int i = 0; i < unavailable.numTargets; i++)
		CHECK(strcmp(unavailable.targets[i].kind, "gpu") != 0);
	CHECK(BenchMetrics::WriteResults());
	puts("bench metrics host test OK");
	return 0;
}
