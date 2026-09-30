// Benchmark measurements: frame records, per-test statistics, analysis and the
// result files (results.json, frames.csv, summary.txt). See benchmark_metrics.h.
// Only the standard library and BenchPlatform are used, so the file is tested
// on the host by tools/vita/check-bench-metrics.py with a fake platform.
// The source stays ASCII: the French accents of summary.txt and results.json
// are UTF-8 octal escapes (FR_* below); BenchTarget::advice itself is kept
// plain ASCII because the results screen draws it with the game font.

#ifdef RELCS_BENCHMARK

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <algorithm>
#include "benchmark_metrics.h"

const char *const gBenchGroupNames[BG_COUNT] = {
	"sim", "audio", "renderlist", "envmap", "scene", "fx", "2d", "eof"
};

#define FR_E  "\303\251"	// e acute
#define FR_EG "\303\250"	// e grave
#define FR_EC "\303\252"	// e circumflex
#define FR_A  "\303\240"	// a grave
#define FR_AC "\303\242"	// a circumflex
#define FR_IC "\303\256"	// i circumflex
#define FR_OC "\303\264"	// o circumflex
#define FR_UG "\303\271"	// u grave
#define FR_UC "\303\273"	// u circumflex
#define FR_C  "\303\247"	// c cedilla
#define FR_EE "\303\211"	// E acute
#define FR_AA "\303\200"	// A grave

namespace {

enum {
	GPU_RING = 32,             // recent frames kept for late GPU completions
	MAX_PLAT_SECTIONS = 256,
	ADVICE_UTF8 = 256,
	MAX_SECTION_TARGETS = 8,
	HITCH_MIN_US = 100000,
	INTERRUPT_US = 2000000,
};
#define SECTION_TARGET_MIN_MS 0.25
#define FEATURE_TARGET_MIN_MS 0.5

struct GpuSlot {
	uint32_t frame;
	int32_t record;            // index in run->frames, -1 = not stored
	bool used, hasSwap, haveDone, haveGpu;
	uint64_t swapBeginUs, doneUs;
	uint32_t gpuUs;
};

struct TestExtra {
	BenchCounters base;        // at BeginTest, re-based at the first StartMeasuring
	bool rebased, ended, dirty;
	uint64_t wall0, wall1;     // NowUs of the thread samples
	uint64_t tBase;            // FrameBegin of the first recorded frame
	uint32_t gpuSyncSamples;
};

struct PendingFrame {
	bool open, record, ended, inSwap, hasSwap;
	uint32_t id;
	uint64_t beginUs, endUs, swapStartUs, firstSwapUs, swapUs;
	uint32_t swaps, gpuSyncUs;
	BenchCounters counters;    // at FrameBegin
	uint32_t groupUs[BG_COUNT];
	float cam[3];
	int peds, vehicles, objects, streamRequests;
	bool paused, gpuSync;
};

static BenchRun *gRun;
static BenchTestResult *gTestsAlloc;
static BenchFrame *gFramesAlloc;
static TestExtra *gExtra;
static double *gValues, *gScratch;
static int gCur = -1;
static bool gMeasuring;
static uint32_t gNextFrameId;
static uint32_t gDropped;
static bool gFolderReady;
static PendingFrame gF;
static GpuSlot gRing[GPU_RING];
static int16_t gSecMap[MAX_PLAT_SECTIONS];   // platform section -> index in the current test
static int8_t gSecGroup[MAX_PLAT_SECTIONS];  // platform section -> BenchGroup, -1 none, -2 not looked up
static char gAdvice[BENCH_MAX_TARGETS][ADVICE_UTF8];   // UTF-8 advice for the files

// ---- small helpers -------------------------------------------------------------

static bool Finite(double v) { return v == v && v < 1e300 && v > -1e300; }
static uint32_t Clamp32(uint64_t v) { return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)v; }
static uint16_t Clamp16(int v) { return v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : (uint16_t)v; }
static bool Eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static bool StartsWith(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

// Copies with truncation on a UTF-8 sequence boundary.
static void Copy(char *dst, int size, const char *src)
{
	if (size <= 0) return;
	if (!src) src = "";
	int i = 0;
	while (i < size - 1 && src[i]) { dst[i] = src[i]; i++; }
	if (src[i])
		while (i > 0 && ((unsigned char)src[i] & 0xC0) == 0x80) i--;
	dst[i] = 0;
}

// UTF-8 French text -> plain ASCII (game font).
static void ToAscii(char *dst, int size, const char *src)
{
	int o = 0;
	const unsigned char *s = (const unsigned char *)src;
	while (*s && o < size - 1) {
		unsigned c = s[0];
		if (c < 0x80) { dst[o++] = (char)c; s++; continue; }
		const char *rep = "";
		int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
		unsigned d = s[1];
		if (c == 0xC3) {
			if (d >= 0xA0 && d <= 0xA5) rep = "a";
			else if (d == 0xA7) rep = "c";
			else if (d >= 0xA8 && d <= 0xAB) rep = "e";
			else if (d >= 0xAC && d <= 0xAF) rep = "i";
			else if (d >= 0xB2 && d <= 0xB6) rep = "o";
			else if (d >= 0xB9 && d <= 0xBC) rep = "u";
			else if (d >= 0x80 && d <= 0x85) rep = "A";
			else if (d == 0x87) rep = "C";
			else if (d >= 0x88 && d <= 0x8B) rep = "E";
			else if (d >= 0x8C && d <= 0x8F) rep = "I";
			else if (d >= 0x92 && d <= 0x96) rep = "O";
			else if (d >= 0x99 && d <= 0x9C) rep = "U";
		} else if (c == 0xC5 && d == 0x93) rep = "oe";
		else if (c == 0xC2) rep = " ";
		else if (c == 0xE2 && d == 0x80) {
			unsigned e = s[2];
			rep = e == 0x99 || e == 0x98 ? "'" : e == 0xA6 ? "..." : "-";
		}
		for (; *rep && o < size - 1; rep++) dst[o++] = *rep;
		for (int k = 0; k < len && *s; k++) s++;
	}
	dst[o] = 0;
}

static void SampleCounters(BenchCounters *c)
{
	memset(c, 0, sizeof(*c));
	BenchPlatform::ReadCounters(c);
}

enum { NUM_COUNTERS = sizeof(BenchCounters) / sizeof(uint64_t) };

static void CounterDelta(const BenchCounters *from, const BenchCounters *to, BenchCounters *out)
{
	const uint64_t *a = (const uint64_t *)from;
	const uint64_t *b = (const uint64_t *)to;
	uint64_t *o = (uint64_t *)out;
	for (int i = 0; i < NUM_COUNTERS; i++)
		o[i] = b[i] >= a[i] ? b[i] - a[i] : 0;
}

static bool TestDone(int i) { return gExtra && i >= 0 && gExtra[i].ended; }

// ---- section to group mapping --------------------------------------------------

static const struct { const char *name; int group; } kGroupMap[] = {
	{ "CGame::Process", BG_SIM },
	{ "DMAudio.Service", BG_AUDIO },
	{ "CnstrRenderList", BG_RENDERLIST }, { "PreRender", BG_RENDERLIST },
	{ "EnvMapBeforeFrame", BG_ENVMAP }, { "EnvMapRender", BG_ENVMAP },
	{ "StartOfFrame", BG_SCENE }, { "RenderScene", BG_SCENE },
	{ "RenderEffects", BG_FX }, { "RenderMotionBlur", BG_FX },
	{ "Render2dStuff", BG_2D }, { "RenderMenus", BG_2D }, { "DoFade", BG_2D },
	{ "Render2dStuff-Fade", BG_2D },
	{ "EndOfFrame", BG_EOF },
};

static int PlatformGroup(int i)
{
	if (gSecGroup[i] == -2) {
		int g = -1;
		const char *name = BenchPlatform::SectionName(i);
		if (name && BenchPlatform::SectionDepth(i) == 0)
			for (size_t k = 0; k < sizeof(kGroupMap) / sizeof(kGroupMap[0]); k++)
				if (Eq(name, kGroupMap[k].name)) { g = kGroupMap[k].group; break; }
		gSecGroup[i] = (int8_t)g;
	}
	return gSecGroup[i];
}

// Index of platform section i in the test's table, created on first sight
// (its parents first, so the parent links point into the same table).
static int TestSection(BenchTestResult *t, int i, int guard)
{
	if (i < 0 || i >= MAX_PLAT_SECTIONS) return -1;
	if (gSecMap[i] >= 0) return gSecMap[i];
	int parent = -1;
	int p = BenchPlatform::SectionParent(i);
	if (p >= 0 && p != i && guard < 32)
		parent = TestSection(t, p, guard + 1);
	if (t->numSections >= BENCH_MAX_SECTIONS) return -1;
	int k = t->numSections++;
	BenchSectionStat *s = &t->sections[k];
	memset(s, 0, sizeof(*s));
	s->platformIndex = (int16_t)i;
	s->parent = (int16_t)parent;
	int depth = BenchPlatform::SectionDepth(i);
	s->depth = (uint8_t)(depth < 0 ? 0 : depth > 255 ? 255 : depth);
	Copy(s->name, sizeof(s->name), BenchPlatform::SectionName(i));
	gSecMap[i] = (int16_t)k;
	return k;
}

static void ReadSections(BenchTestResult *t)
{
	int n = BenchPlatform::SectionCount();
	if (n > MAX_PLAT_SECTIONS) n = MAX_PLAT_SECTIONS;
	for (int i = 0; i < n; i++) {
		uint32_t us = BenchPlatform::SectionFrameUs(i);
		uint32_t calls = BenchPlatform::SectionFrameCalls(i);
		if (us == 0 && calls == 0) continue;
		int k = TestSection(t, i, 0);
		if (k >= 0) {
			BenchSectionStat *s = &t->sections[k];
			s->totalUs += us;
			s->calls += calls;
			s->framesPresent++;
			if (us > s->maxFrameUs) s->maxFrameUs = us;
		}
		int g = PlatformGroup(i);
		if (g >= 0) gF.groupUs[g] += us;
	}
}

// ---- GPU completions -----------------------------------------------------------

static void UpdateGpu(uint32_t k)
{
	GpuSlot *s = &gRing[k % GPU_RING];
	if (!s->used || s->frame != k || !s->hasSwap || !s->haveDone) return;
	// the GPU starts frame k once frame k-1 is done and k has been submitted
	uint64_t prev = 0;
	bool found = false;
	for (uint32_t j = 1; j < GPU_RING && j <= k; j++) {
		const GpuSlot *p = &gRing[(k - j) % GPU_RING];
		if (!p->used || p->frame != k - j) break;
		if (!p->hasSwap) continue;
		if (!p->haveDone) return;
		prev = p->doneUs;
		found = true;
		break;
	}
	if (!found) return;
	uint64_t start = std::max(prev, s->swapBeginUs);
	uint32_t gpu = s->doneUs > start ? Clamp32(s->doneUs - start) : 0;
	s->gpuUs = gpu ? gpu : 1;	// 0 means unknown
	s->haveGpu = true;
	if (s->record >= 0 && (uint32_t)s->record < gRun->numFrames) {
		BenchFrame *b = &gRun->frames[s->record];
		b->gpuUs = s->gpuUs;
		if (b->test < BENCH_MAX_TESTS && gExtra[b->test].ended)
			gExtra[b->test].dirty = true;
	}
}

static void DrainGpu(void)
{
	uint32_t frame;
	uint64_t done;
	for (int guard = 0; guard < 256; guard++) {
		frame = 0;
		done = 0;
		if (!BenchPlatform::PollGpuDone(&frame, &done)) break;
		GpuSlot *s = &gRing[frame % GPU_RING];
		if (!s->used || s->frame != frame) continue;
		if (!s->haveDone || done > s->doneUs) {
			s->doneUs = done;
			s->haveDone = true;
		}
		UpdateGpu(frame);
		// later frames may have been waiting for this one
		for (uint32_t k = frame + 1; k != gNextFrameId && k - frame < GPU_RING; k++) {
			const GpuSlot *n = &gRing[k % GPU_RING];
			if (!n->used || n->frame != k) break;
			if (!n->hasSwap) continue;
			if (!n->haveDone) break;
			UpdateGpu(k);
		}
	}
}

// ---- frame recording -----------------------------------------------------------

static void CloseFrame(uint64_t endUs, const BenchCounters *now)
{
	PendingFrame *f = &gF;
	f->open = false;
	if (!f->record || gCur < 0) return;
	BenchRun *r = gRun;
	if (r->numFrames >= r->maxFrames) { gDropped++; return; }
	BenchTestResult *t = &r->tests[gCur];
	uint32_t idx = r->numFrames++;
	BenchFrame *b = &r->frames[idx];
	memset(b, 0, sizeof(*b));
	if (t->frameCount == 0) {
		t->firstFrame = idx;
		gExtra[gCur].tBase = f->beginUs;
	}
	b->test = (uint16_t)gCur;
	b->index = t->frameCount++;
	uint64_t base = gExtra[gCur].tBase;
	b->tUs = Clamp32(f->beginUs >= base ? f->beginUs - base : 0);
	b->frameUs = Clamp32(endUs > f->beginUs ? endUs - f->beginUs : 0);
	uint64_t workEnd = f->hasSwap ? f->firstSwapUs : f->ended ? f->endUs : endUs;
	b->workUs = Clamp32(workEnd > f->beginUs ? workEnd - f->beginUs : 0);
	b->swapUs = Clamp32(f->swapUs);
	if (f->swaps == 0) b->flags |= BF_NO_SWAP;
	if (f->swaps > 1) b->flags |= BF_EXTRA_SWAP;
	if (f->paused) b->flags |= BF_PAUSED;
	if (f->gpuSync) {
		b->flags |= BF_GPU_SYNC;
		b->gpuSyncUs = f->gpuSyncUs;
	}
	memcpy(b->groupUs, f->groupUs, sizeof(b->groupUs));
	BenchCounters d;
	CounterDelta(&f->counters, now, &d);
	b->draws = Clamp32(d.draws);
	b->indices = Clamp32(d.indices);
	b->uniforms = Clamp32(d.uniforms);
	b->texBinds = Clamp32(d.texBinds);
	b->uploadBytes = Clamp32(d.vertexUploadBytes + d.indexUploadBytes + d.textureUploadBytes);
	b->diskBytes = Clamp32(d.diskBytes);
	b->peds = Clamp16(f->peds);
	b->vehicles = Clamp16(f->vehicles);
	b->objects = Clamp16(f->objects);
	b->streamRequests = Clamp16(f->streamRequests);
	b->camX = f->cam[0];
	b->camY = f->cam[1];
	b->camZ = f->cam[2];
	GpuSlot *s = &gRing[f->id % GPU_RING];
	if (s->used && s->frame == f->id) {
		s->record = (int32_t)idx;
		if (s->haveGpu) b->gpuUs = s->gpuUs;
	}
}

// ---- per-test statistics -------------------------------------------------------

static int Rank(int pct, int n)
{
	int r = (int)(((long long)pct * n + 99) / 100);
	if (r < 1) r = 1;
	if (r > n) r = n;
	return r - 1;
}

static void ComputeTestStats(int ti)
{
	BenchRun *r = gRun;
	BenchTestResult *t = &r->tests[ti];
	TestExtra *e = &gExtra[ti];
	uint32_t n = t->frameCount;
	BenchFrame *f = r->frames + t->firstFrame;
	if (t->firstFrame + n > r->numFrames) n = 0;

	memset(&t->frameMs, 0, sizeof(BenchStats));
	memset(&t->workMs, 0, sizeof(BenchStats));
	memset(&t->swapMs, 0, sizeof(BenchStats));
	memset(&t->gpuMs, 0, sizeof(BenchStats));
	memset(&t->gpuSyncMs, 0, sizeof(BenchStats));
	t->seconds = t->fps = 0;
	t->gpuSamples = 0;
	t->cpuBoundPct = t->gpuBoundPct = -1;
	memset(t->groupMs, 0, sizeof(t->groupMs));
	t->hitches = 0;
	t->worstHitchMs = 0;
	t->worstHitchGroup[0] = 0;
	t->draws = t->indices = t->uniforms = t->texBinds = t->uploadKiB = t->diskKiB = 0;
	t->peds = t->vehicles = t->objects = 0;
	e->gpuSyncSamples = 0;

	// section self times: inclusive minus direct children
	for (int i = 0; i < t->numSections; i++) {
		double self = t->sections[i].totalUs;
		for (int c = 0; c < t->numSections; c++)
			if (t->sections[c].parent == i) self -= t->sections[c].totalUs;
		t->sections[i].selfUs = self > 0 ? self : 0;
	}
	if (n == 0) return;

	double sumFrame = 0;
	double groups[BG_COUNT] = { 0 };
	double draws = 0, indices = 0, uniforms = 0, texBinds = 0, upload = 0, disk = 0;
	double peds = 0, vehicles = 0, objects = 0;
	uint32_t cpuBound = 0, gpuBound = 0;
	bool longFrame = false;
	for (uint32_t k = 0; k < n; k++) {
		BenchFrame *b = &f[k];
		b->flags &= ~BF_HITCH;
		sumFrame += b->frameUs;
		for (int g = 0; g < BG_COUNT; g++) groups[g] += b->groupUs[g];
		draws += b->draws; indices += b->indices; uniforms += b->uniforms; texBinds += b->texBinds;
		upload += b->uploadBytes; disk += b->diskBytes;
		peds += b->peds; vehicles += b->vehicles; objects += b->objects;
		// Swap can also wait for the display queue, GC and driver work. It is
		// not a GPU timer. Classify only frames with a completed GPU sample.
		if (b->gpuUs) {
			if (b->gpuUs > b->workUs) gpuBound++; else cpuBound++;
		}
		if (b->frameUs > INTERRUPT_US) longFrame = true;
	}
	t->seconds = sumFrame / 1e6;
	t->fps = t->seconds > 0 ? n / t->seconds : 0;
	uint32_t classified = cpuBound + gpuBound;
	if (classified) {
		t->cpuBoundPct = 100.0 * cpuBound / classified;
		t->gpuBoundPct = 100.0 * gpuBound / classified;
	}
	for (int g = 0; g < BG_COUNT; g++) t->groupMs[g] = groups[g] / n / 1000.0;
	t->draws = draws / n; t->indices = indices / n; t->uniforms = uniforms / n;
	t->texBinds = texBinds / n; t->uploadKiB = upload / n / 1024.0; t->diskKiB = disk / n / 1024.0;
	t->peds = peds / n; t->vehicles = vehicles / n; t->objects = objects / n;
	// a suspend (PS button) shows up as one huge frame; streaming tests may
	// legitimately block that long on a load
	if (longFrame && t->kind != BTK_STREAMING && t->kind != BTK_TELEPORT)
		t->interrupted = true;

	double *v = gValues;
	for (uint32_t k = 0; k < n; k++) v[k] = f[k].frameUs / 1000.0;
	BenchMetrics::ComputeStats(v, n, gScratch, &t->frameMs);
	for (uint32_t k = 0; k < n; k++) v[k] = f[k].workUs / 1000.0;
	BenchMetrics::ComputeStats(v, n, gScratch, &t->workMs);
	for (uint32_t k = 0; k < n; k++) v[k] = f[k].swapUs / 1000.0;
	BenchMetrics::ComputeStats(v, n, gScratch, &t->swapMs);
	uint32_t m = 0;
	for (uint32_t k = 0; k < n; k++)
		if (f[k].gpuUs) v[m++] = f[k].gpuUs / 1000.0;
	t->gpuSamples = m;
	if (m) BenchMetrics::ComputeStats(v, m, gScratch, &t->gpuMs);
	m = 0;
	for (uint32_t k = 0; k < n; k++)
		if (f[k].flags & BF_GPU_SYNC) v[m++] = f[k].gpuSyncUs / 1000.0;
	e->gpuSyncSamples = m;
	if (m) BenchMetrics::ComputeStats(v, m, gScratch, &t->gpuSyncMs);

	double threshold = std::max(HITCH_MIN_US / 1000.0, 3.0 * t->frameMs.median);
	int worst = -1;
	for (uint32_t k = 0; k < n; k++) {
		if (f[k].frameUs / 1000.0 <= threshold) continue;
		f[k].flags |= BF_HITCH;
		t->hitches++;
		if (worst < 0 || f[k].frameUs > f[worst].frameUs) worst = (int)k;
	}
	if (worst >= 0) {
		t->worstHitchMs = f[worst].frameUs / 1000.0;
		int g = -1;
		for (int i = 0; i < BG_COUNT; i++)
			if (f[worst].groupUs[i] && (g < 0 || f[worst].groupUs[i] > f[worst].groupUs[g])) g = i;
		Copy(t->worstHitchGroup, sizeof(t->worstHitchGroup), g >= 0 ? gBenchGroupNames[g] : "");
	}
}

static void RecomputeDirty(void)
{
	if (!gRun) return;
	for (int i = 0; i < gRun->numTests; i++)
		if (gExtra[i].ended && gExtra[i].dirty) {
			gExtra[i].dirty = false;
			ComputeTestStats(i);
		}
}

// ---- analysis ------------------------------------------------------------------

enum { V_DIAG = 1, V_INVERT = 2 };
static const struct { const char *id; const char *label; int flags; const char *advice; } kVariants[] = {
	{ "baseline", "r" FR_E "f" FR_E "rence", V_DIAG, "" },
	{ "no_peds", "sans pi" FR_E "tons", 0,
	  "Les pi" FR_E "tons co" FR_UC "tent cher (IA, animation, skinning) : r" FR_E "duire la densit" FR_E " ou all" FR_E "ger leur rendu." },
	{ "no_vehicles", "sans v" FR_E "hicules", 0,
	  "Les v" FR_E "hicules co" FR_UC "tent cher : LOD plus agressif, moins de trafic." },
	{ "no_objects", "sans objets", 0,
	  "Objets et d" FR_E "cors : r" FR_E "duire leur distance d'affichage, regrouper les draws." },
	{ "no_world_lod", "sans b" FR_AC "timents LOD", 0,
	  "B" FR_AC "timents LOD lointains : simplifier les LOD ou r" FR_E "duire leur distance." },
	{ "no_world_opaque", "sans b" FR_AC "timents opaques", 0,
	  "B" FR_AC "timents opaques : moins de draw calls (regroupement) et un culling plus agressif." },
	{ "no_world_transparent", "sans d" FR_E "cor transparent", 0,
	  "D" FR_E "cor transparent : limiter le m" FR_E "lange alpha et le surdessin." },
	{ "no_water", "sans eau", 0,
	  "Eau : r" FR_E "duire la zone d'eau anim" FR_E "e, simplifier la passe transparente." },
	{ "no_sky", "sans ciel", 0, "Ciel et nuages : simplifier leur rendu." },
	{ "no_shadows", "sans ombres", 0, "Ombres : en r" FR_E "duire le nombre ou la distance." },
	{ "no_coronas", "sans coronas", 0, "Coronas : limiter leur nombre et les tests de visibilit" FR_E "." },
	{ "no_particles_fx", "sans particules ni effets", 0,
	  "Particules et effets divers : plafonner leur nombre, r" FR_E "duire le surdessin." },
	{ "no_envmap", "sans carte d'environnement", 0,
	  "Carte d'environnement : la mettre " FR_A " jour une image sur N ou r" FR_E "duire sa taille." },
	{ "no_postfx", "sans filtre couleur", 0,
	  "Filtre couleur plein " FR_E "cran : fusionner les passes ou le simplifier." },
	{ "hud_on", "avec HUD", V_INVERT, "Le HUD co" FR_UC "te : regrouper ses sprites et son texte." },
	{ "ps2_alpha_off", "alpha PS2 d" FR_E "sactiv" FR_E, 0,
	  "Le mode alpha PS2 (double passe) co" FR_UC "te : le r" FR_E "server aux mat" FR_E "riaux qui en ont besoin." },
	{ "backface_cull", FR_E "limination des faces arri" FR_EG "re", 0,
	  "Activer l'" FR_E "limination des faces arri" FR_EG "re fait gagner du temps." },
	{ "lod_low", "distance d'affichage 0.925", 0,
	  "Une distance d'affichage plus courte fait gagner du temps : ajuster la valeur par d" FR_E "faut." },
	{ "lod_high", "distance d'affichage 1.8", V_INVERT,
	  "Une distance d'affichage plus longue co" FR_UC "te cher : garder une valeur mod" FR_E "r" FR_E "e." },
	{ "farclip_300", "plan lointain 300 m", 0,
	  "Un plan lointain " FR_A " 300 m fait gagner du temps : r" FR_E "duire la distance de vue." },
	{ "viewport_75", "zone 3D 75 %", V_DIAG, "" },
	{ "viewport_50", "zone 3D 50 %", V_DIAG, "" },
	{ "viewport_25", "zone 3D 25 %", V_DIAG, "" },
	{ "viewport_min", "zone 3D minimale", V_DIAG, "" },
	{ "flat_textures", "textures unies 1x1", 0,
	  "Les textures co" FR_UC "tent (bande passante) : compresser, r" FR_E "duire leur taille, utiliser les mipmaps." },
	{ "draw_tiny", "draws de 3 indices", V_DIAG, "" },
	{ "draw_null", "draws annul" FR_E "s", V_DIAG, "" },
	{ "gpu_sync", "attente GPU apr" FR_EG "s le swap", V_DIAG, "" },
	{ "no_render_3d", "sans rendu 3D", V_DIAG, "" },
	{ "baseline_end", "r" FR_E "f" FR_E "rence de fin (d" FR_E "rive)", V_DIAG, "" },
	{ "sim_running", "simulation active", V_DIAG, "" },
	{ "no_audio", "sans audio", 0,
	  "L'audio co" FR_UC "te sur le thread principal : d" FR_E "placer le travail vers un thread audio." },
};

static int FindVariant(const char *id)
{
	for (size_t i = 0; i < sizeof(kVariants) / sizeof(kVariants[0]); i++)
		if (Eq(id, kVariants[i].id)) return (int)i;
	return -1;
}

static const struct { const char *name; const char *advice; } kSectionAdvice[] = {
	{ "CGame::Process", "Temps non attribu" FR_E " de la simulation (cam" FR_E "ra, trafic, armes, particules...) : ajouter des sections, all" FR_E "ger le plus co" FR_UC "teux." },
	{ "World::Process", "Mise " FR_A " jour du monde : voir World.Control, Collision et Anim ; moins d'entit" FR_E "s actives." },
	{ "World.Control", "IA des pi" FR_E "tons et physique des v" FR_E "hicules : r" FR_E "duire la densit" FR_E ", espacer les mises " FR_A " jour lointaines." },
	{ "World.Collision", "Collisions : moins de sous-pas pour les entit" FR_E "s lointaines, sortie rapide pour les objets immobiles." },
	{ "World.Anim", "Animations : mettre " FR_A " jour moins souvent les pi" FR_E "tons " FR_E "loign" FR_E "s ou hors " FR_E "cran." },
	{ "Streaming::Update", "Streaming : " FR_E "viter les chargements synchrones, " FR_E "taler les conversions sur plusieurs images." },
	{ "Streaming::LoadAll", "Chargement bloquant (LoadAll) : pr" FR_E "charger plus t" FR_OC "t ou passer en requ" FR_EC "tes asynchrones." },
	{ "Streaming::Convert", "Conversion des mod" FR_EG "les sur le thread principal : d" FR_E "couper (v" FR_E "hicules, TXD, COL) sur plusieurs images." },
	{ "TheScripts::Process", "Scripts : un LoadAll d" FR_E "clench" FR_E " par script bloque l'image ; diff" FR_E "rer ces chargements." },
	{ "Population::Update", "Population : limiter les cr" FR_E "ations de pi" FR_E "tons et de v" FR_E "hicules par image." },
	{ "DMAudio.Service", "Audio sur le thread principal (lectures disque, d" FR_E "codage) : d" FR_E "placer vers un thread audio." },
	{ "CnstrRenderList", "Liste de rendu (ScanWorld) : r" FR_E "duire la distance d'affichage ou am" FR_E "liorer le culling." },
	{ "PreRender", "PreRender (os des pi" FR_E "tons, ombres) : moins de pi" FR_E "tons visibles, matrices d'os moins fr" FR_E "quentes." },
	{ "EnvMapBeforeFrame", "Carte d'environnement (FBO 512x512 " FR_A " chaque image) : mise " FR_A " jour une image sur N ou plus petite." },
	{ "EnvMapRender", "Rendu de la carte d'environnement : le r" FR_E "server aux v" FR_E "hicules proches." },
	{ "StartOfFrame", "D" FR_E "but d'image (clear, ciel de fond, CameraSize) : " FR_E "viter le travail refait " FR_A " chaque image." },
	{ "RenderScene", "Rendu de la sc" FR_EG "ne : voir les sous-sections Scene.*." },
	{ "Scene.Sky", "Ciel et nuages : moins de g" FR_E "om" FR_E "trie ou un rendu simplifi" FR_E "." },
	{ "Scene.Water", "Eau : r" FR_E "duire la zone d'eau anim" FR_E "e, simplifier la passe transparente." },
	{ "Scene.World0", "Routes et grands b" FR_AC "timents (LOD) : regrouper les draws, r" FR_E "duire les changements d'" FR_E "tat." },
	{ "Scene.World1Opaque", "B" FR_AC "timents opaques : moins de draw calls (regroupement), culling plus agressif." },
	{ "Scene.EverythingBarRoads", "Objets et d" FR_E "cors : r" FR_E "duire la distance des petits objets, regrouper les draws." },
	{ "Scene.FadingIn", "Entit" FR_E "s en fondu : raccourcir les fondus." },
	{ "RenderEffects", "Effets : voir les sous-sections FX.*." },
	{ "FX.World2Transparent", "Objets transparents (passes ADD/BLEND) : limiter le m" FR_E "lange alpha et le surdessin." },
	{ "FX.Vehicles", "V" FR_E "hicules et pi" FR_E "tons (skinning, 64 matrices d'os par atome) : n'envoyer que les os utiles, LOD." },
	{ "FX.Shadows", "Ombres : en r" FR_E "duire le nombre ou la distance." },
	{ "FX.Coronas", "Coronas : limiter leur nombre et les tests de visibilit" FR_E "." },
	{ "FX.Particles", "Particules : plafonner leur nombre, r" FR_E "duire le surdessin." },
	{ "RenderMotionBlur", "Filtre couleur plein " FR_E "cran (jusqu'" FR_A " 4 quads m" FR_E "lang" FR_E "s) : une seule passe." },
	{ "Render2dStuff", "HUD et 2D : regrouper le texte et les sprites." },
	{ "EndOfFrame", "Fin d'image (swap) : attente du GPU ou de vitaGL ; voir le temps GPU et les tests viewport." },
};

static const char *SectionAdvice(const char *name)
{
	for (size_t i = 0; i < sizeof(kSectionAdvice) / sizeof(kSectionAdvice[0]); i++)
		if (Eq(name, kSectionAdvice[i].name)) return kSectionAdvice[i].advice;
	return "Section co" FR_UC "teuse : mesurer ses sous-parties et r" FR_E "duire le travail par image.";
}

// iso_<viewpoint>_<variant> with viewpoint "<letter>_<name>", when the
// director left the fields empty.
static void FillIsolationIds(void)
{
	for (int i = 0; i < gRun->numTests; i++) {
		BenchTestResult *t = &gRun->tests[i];
		if (t->viewpoint[0] && t->variant[0]) continue;
		if (!StartsWith(t->id, "iso_")) continue;
		const char *s = t->id + 4;
		const char *u1 = strchr(s, '_');
		const char *u2 = u1 ? strchr(u1 + 1, '_') : NULL;
		if (!u2 || !u2[1]) continue;
		int len = (int)(u2 - s);
		if (len >= (int)sizeof(t->viewpoint)) continue;
		memcpy(t->viewpoint, s, len);
		t->viewpoint[len] = 0;
		Copy(t->variant, sizeof(t->variant), u2 + 1);
	}
}

static const char *DeltaReference(const char *variant)
{
	return Eq(variant, "no_audio") ? "sim_running" : "baseline";
}

static bool Usable(int i)
{
	const BenchTestResult *t = &gRun->tests[i];
	return TestDone(i) && !t->skipped && t->frameCount > 0;
}

static void ComputeDeltas(void)
{
	BenchRun *r = gRun;
	r->numDeltas = 0;
	for (int i = 0; i < r->numTests && r->numDeltas < BENCH_MAX_DELTAS; i++) {
		const BenchTestResult *v = &r->tests[i];
		if (!v->viewpoint[0] || !v->variant[0] || Eq(v->variant, "baseline")) continue;
		if (!Usable(i)) continue;
		const char *refName = DeltaReference(v->variant);
		int ref = -1;
		for (int j = 0; j < r->numTests; j++) {
			const BenchTestResult *c = &r->tests[j];
			if (j == i || !TestDone(j) || !Eq(c->viewpoint, v->viewpoint) || !Eq(c->variant, refName)) continue;
			if (ref < 0 || j < i) ref = j;	// the latest one before the variant
		}
		if (ref < 0 || !Usable(ref)) continue;
		const BenchTestResult *b = &r->tests[ref];
		BenchDelta *d = &r->deltas[r->numDeltas++];
		memset(d, 0, sizeof(*d));
		Copy(d->viewpoint, sizeof(d->viewpoint), v->viewpoint);
		Copy(d->variant, sizeof(d->variant), v->variant);
		// medians: a single stall (log write, GC) must not move a 90-frame delta
		d->baselineMs = b->frameMs.median;
		d->variantMs = v->frameMs.median;
		d->savedMs = d->baselineMs - d->variantMs;
		d->savedPct = d->baselineMs > 0 ? 100.0 * d->savedMs / d->baselineMs : 0;
		d->baselineGpuMs = b->gpuSamples ? b->gpuMs.median : -1;
		d->variantGpuMs = v->gpuSamples ? v->gpuMs.median : -1;
		d->baselineWorkMs = b->workMs.median;
		d->variantWorkMs = v->workMs.median;
	}
}

struct Candidate {
	BenchTarget t;
	char advice[ADVICE_UTF8];
};

static void AddCandidate(Candidate *c, int *n, int max, const char *what, const char *kind,
                         double ms, double pct, const char *advice)
{
	if (*n >= max || !Finite(ms)) return;
	Candidate *k = &c[(*n)++];
	memset(k, 0, sizeof(*k));
	Copy(k->t.what, sizeof(k->t.what), what);
	Copy(k->t.kind, sizeof(k->t.kind), kind);
	k->t.ms = ms;
	k->t.pct = Finite(pct) ? pct : 0;
	Copy(k->advice, sizeof(k->advice), advice);
}

static bool Gameplay(const BenchTestResult *t)
{
	return Eq(t->group, "graphics") || t->kind == BTK_FOLLOW;	// ids get _L2... with Loops
}

static bool Scored(int i)
{
	return Usable(i) && !gRun->tests[i].interrupted && gRun->tests[i].fps > 0;
}

static void ComputeTargets(void)
{
	enum { MAX_CAND = 64, MAX_AGG = 256 };
	static Candidate cand[MAX_CAND];
	static struct { char name[32]; double selfUs; } agg[MAX_AGG];
	int nc = 0, na = 0;
	BenchRun *r = gRun;
	char text[ADVICE_UTF8];

	// (a) section self times over the gameplay-like tests, weighted by frames
	double frames = 0, frameUs = 0;
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *t = &r->tests[i];
		if (!Scored(i) || !Gameplay(t)) continue;
		frames += t->frameCount;
		frameUs += t->seconds * 1e6;
		for (int s = 0; s < t->numSections; s++) {
			int k = 0;
			while (k < na && !Eq(agg[k].name, t->sections[s].name)) k++;
			if (k == na) {
				if (na >= MAX_AGG) continue;
				Copy(agg[na].name, sizeof(agg[na].name), t->sections[s].name);
				agg[na++].selfUs = 0;
			}
			agg[k].selfUs += t->sections[s].selfUs;
		}
	}
	double meanFrameMs = frames > 0 ? frameUs / frames / 1000.0 : 0;
	for (int picked = 0; picked < MAX_SECTION_TARGETS && frames > 0; picked++) {
		int best = -1;
		for (int k = 0; k < na; k++)
			if (agg[k].selfUs >= 0 && (best < 0 || agg[k].selfUs > agg[best].selfUs)) best = k;
		if (best < 0) break;
		double ms = agg[best].selfUs / frames / 1000.0;
		agg[best].selfUs = -1;
		if (ms < SECTION_TARGET_MIN_MS) break;
		AddCandidate(cand, &nc, MAX_CAND, agg[best].name, "section", ms,
		             meanFrameMs > 0 ? 100.0 * ms / meanFrameMs : 0, SectionAdvice(agg[best].name));
	}

	// (b) features from the isolation deltas, averaged over the viewpoints
	const int numVariants = (int)(sizeof(kVariants) / sizeof(kVariants[0]));
	for (int v = 0; v < numVariants; v++) {
		double saved = 0, pct = 0;
		int count = 0;
		for (int d = 0; d < r->numDeltas; d++)
			if (Eq(r->deltas[d].variant, kVariants[v].id)) {
				saved += r->deltas[d].savedMs;
				pct += r->deltas[d].savedPct;
				count++;
			}
		if (!count) continue;
		saved /= count;
		pct /= count;
		if (Eq(kVariants[v].id, "sim_running")) {
			// negative saving = what the running simulation costs
			double variantMs = 0;
			for (int d = 0; d < r->numDeltas; d++)
				if (Eq(r->deltas[d].variant, "sim_running")) variantMs += r->deltas[d].variantMs;
			variantMs /= count;
			if (-saved >= FEATURE_TARGET_MIN_MS)
				AddCandidate(cand, &nc, MAX_CAND, "simulation", "cpu", -saved,
				             variantMs > 0 ? 100.0 * -saved / variantMs : 0,
				             "Simulation (IA, physique, scripts, streaming) : voir World.Control et CGame::Process dans les sections.");
			continue;
		}
		if (kVariants[v].flags & V_DIAG) continue;
		if (kVariants[v].flags & V_INVERT) { saved = -saved; pct = -pct; }
		if (saved >= FEATURE_TARGET_MIN_MS)
			AddCandidate(cand, &nc, MAX_CAND, kVariants[v].id, "feature", saved, pct, kVariants[v].advice);
	}

	// (c) GPU bound graphics tests
	double gFrames = 0, gBound = 0, gpuSum = 0, gpuN = 0, gFrameUs = 0;
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *t = &r->tests[i];
		if (!Scored(i) || !Eq(t->group, "graphics")) continue;
		gFrames += t->frameCount;
		gFrameUs += t->seconds * 1e6;
		if (t->gpuSamples) gBound += t->gpuBoundPct * t->gpuSamples / 100.0;
		gpuSum += t->gpuMs.mean * t->gpuSamples;
		gpuN += t->gpuSamples;
	}
	if (gFrames > 0 && gpuN > 0 && gBound > 0.5 * gpuN) {
		double ms = gpuSum / gpuN;
		double frameMs = gFrameUs / gFrames / 1000.0;
		double vpPct = 0;
		int vpCount = 0;
		for (int d = 0; d < r->numDeltas; d++)
			if (Eq(r->deltas[d].variant, "viewport_50")) { vpPct += r->deltas[d].savedPct; vpCount++; }
		if (!vpCount)
			snprintf(text, sizeof(text), "GPU limitant sur %.0f %% des images : lancer viewport_50 pour distinguer remplissage et g" FR_E "om" FR_E "trie.",
			         100.0 * gBound / gpuN);
		else if (vpPct / vpCount > 25.0)
			snprintf(text, sizeof(text), "GPU limit" FR_E " par le remplissage (viewport 50 %% : -%.0f %%) : moins de surdessin, de transparence et de passes plein " FR_E "cran.",
			         vpPct / vpCount);
		else
			snprintf(text, sizeof(text), "GPU limit" FR_E " par la g" FR_E "om" FR_E "trie ou les " FR_E "tats (viewport 50 %% : -%.0f %%) : moins de draws, de sommets et de changements d'" FR_E "tat.",
			         vpPct / vpCount);
		AddCandidate(cand, &nc, MAX_CAND, "GPU", "gpu", ms,
		             frameMs > 0 ? 100.0 * ms / frameMs : 0, text);
	}

	// (d) streaming: worst hitch of the bridge run, slowest teleport load
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *t = &r->tests[i];
		if (!Usable(i) || t->kind != BTK_STREAMING || !t->hitches) continue;
		double hitchUs = 0;
		const BenchFrame *f = r->frames + t->firstFrame;
		for (uint32_t k = 0; k < t->frameCount; k++)
			if (f[k].flags & BF_HITCH) hitchUs += f[k].frameUs;
		snprintf(text, sizeof(text), "Pic de %.0f ms (groupe %s) : chargement bloquant (LoadAll, changement d'" FR_IC "le) ; pr" FR_E "charger ou rendre asynchrone.",
		         t->worstHitchMs, t->worstHitchGroup[0] ? t->worstHitchGroup : "?");
		AddCandidate(cand, &nc, MAX_CAND, t->id, "streaming", t->worstHitchMs,
		             t->seconds > 0 ? 100.0 * hitchUs / (t->seconds * 1e6) : 0, text);
		break;
	}
	int worstTp = -1;
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *t = &r->tests[i];
		if (!TestDone(i) || t->skipped || t->kind != BTK_TELEPORT || !(t->loadMs > 0)) continue;
		if (worstTp < 0 || t->loadMs > r->tests[worstTp].loadMs) worstTp = i;
	}
	if (worstTp >= 0) {
		const BenchTestResult *t = &r->tests[worstTp];
		snprintf(text, sizeof(text), "LoadScene jusqu'" FR_A " %.0f ms (%s) : moins de lectures disque, conversions hors du thread principal.",
		         t->loadMs, t->id);
		AddCandidate(cand, &nc, MAX_CAND, "teleportation", "streaming", t->loadMs, 0, text);
	}

	// sort by cost (stable), keep the first BENCH_MAX_TARGETS
	for (int i = 1; i < nc; i++) {
		Candidate tmp = cand[i];
		int j = i - 1;
		while (j >= 0 && cand[j].t.ms < tmp.t.ms) { cand[j + 1] = cand[j]; j--; }
		cand[j + 1] = tmp;
	}
	r->numTargets = nc < BENCH_MAX_TARGETS ? nc : BENCH_MAX_TARGETS;
	for (int i = 0; i < r->numTargets; i++) {
		r->targets[i] = cand[i].t;
		ToAscii(r->targets[i].advice, sizeof(r->targets[i].advice), cand[i].advice);
		Copy(gAdvice[i], ADVICE_UTF8, cand[i].advice);
	}
}

static double GeoMeanScore(int which)	// 0 graphics, 1 cpu, 2 overall
{
	double logSum = 0;
	int n = 0;
	for (int i = 0; i < gRun->numTests; i++) {
		const BenchTestResult *t = &gRun->tests[i];
		if (!Scored(i)) continue;
		bool g = Eq(t->group, "graphics"), c = Eq(t->group, "cpu");
		bool s = Eq(t->group, "streaming") && t->kind == BTK_STREAMING;
		if (which == 0 ? !g : which == 1 ? !c : !(g || c || s)) continue;
		logSum += log(t->fps);
		n++;
	}
	return n ? 100.0 * exp(logSum / n) : 0;
}

static void ComputeScores(void)
{
	BenchScores *s = &gRun->scores;
	s->graphics = GeoMeanScore(0);
	s->cpu = GeoMeanScore(1);
	s->overall = GeoMeanScore(2);
	s->valid = s->graphics > 0 && s->cpu > 0;
}

static void AnalyseAll(void)
{
	DrainGpu();
	RecomputeDirty();
	FillIsolationIds();
	ComputeDeltas();
	ComputeTargets();
	ComputeScores();
}

// ---- output buffer -------------------------------------------------------------

struct Buf {
	char *p;
	uint32_t len, cap;
	bool fail;
};

static bool Grow(Buf *b, uint32_t need)
{
	if (b->fail) return false;
	if ((uint64_t)b->len + need + 1 <= b->cap) return true;
	uint64_t cap = b->cap ? b->cap : 4096;
	while (cap < (uint64_t)b->len + need + 1) cap *= 2;
	if (cap > 0x7FFFFFFF) { b->fail = true; return false; }
	char *p = (char *)realloc(b->p, (size_t)cap);
	if (!p) { b->fail = true; return false; }
	b->p = p;
	b->cap = (uint32_t)cap;
	return true;
}

static void PutN(Buf *b, const char *s, uint32_t n)
{
	if (!Grow(b, n)) return;
	memcpy(b->p + b->len, s, n);
	b->len += n;
	b->p[b->len] = 0;
}

static void Put(Buf *b, const char *s) { PutN(b, s, (uint32_t)strlen(s)); }

#ifdef __GNUC__
static void Putf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void Putf(Buf *b, const char *fmt, ...)
{
	if (!Grow(b, 256)) return;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
	va_end(ap);
	if (n < 0) { b->fail = true; return; }
	if ((uint32_t)n >= b->cap - b->len) {
		if (!Grow(b, (uint32_t)n)) return;
		va_start(ap, fmt);
		vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
		va_end(ap);
	}
	b->len += (uint32_t)n;
}

static void U64(char *out, uint64_t v)	// printf's %llu is not everywhere
{
	char tmp[24];
	int n = 0;
	do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
	for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
	out[n] = 0;
}

// ---- results.json --------------------------------------------------------------

static void JStr(Buf *b, const char *s)
{
	PutN(b, "\"", 1);
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		switch (*p) {
		case '"': Put(b, "\\\""); break;
		case '\\': Put(b, "\\\\"); break;
		case '\n': Put(b, "\\n"); break;
		case '\r': Put(b, "\\r"); break;
		case '\t': Put(b, "\\t"); break;
		default:
			if (*p < 0x20) Putf(b, "\\u%04x", (unsigned)*p);
			else PutN(b, (const char *)p, 1);
		}
	}
	PutN(b, "\"", 1);
}

static void JKey(Buf *b, const char *key) { PutN(b, "\"", 1); Put(b, key); Put(b, "\":"); }
static void JNum(Buf *b, double v) { if (Finite(v)) Putf(b, "%.3f", v); else Put(b, "null"); }
static void JNumOr(Buf *b, double v, bool present) { if (present) JNum(b, v); else Put(b, "null"); }
static void JU64(Buf *b, uint64_t v) { char s[24]; U64(s, v); Put(b, s); }
static void JInt(Buf *b, long long v) { if (v < 0) { Put(b, "-"); v = -v; } JU64(b, (uint64_t)v); }
static void JBool(Buf *b, bool v) { Put(b, v ? "true" : "false"); }

static void JStats(Buf *b, const char *key, const BenchStats *s, bool present)
{
	JKey(b, key);
	if (!present) { Put(b, "null"); return; }
	Put(b, "{\"mean\":"); JNum(b, s->mean);
	Put(b, ",\"median\":"); JNum(b, s->median);
	Put(b, ",\"p90\":"); JNum(b, s->p90);
	Put(b, ",\"p95\":"); JNum(b, s->p95);
	Put(b, ",\"p99\":"); JNum(b, s->p99);
	Put(b, ",\"min\":"); JNum(b, s->min);
	Put(b, ",\"max\":"); JNum(b, s->max);
	Put(b, ",\"low1_fps\":"); JNum(b, s->low1Fps);
	Put(b, "}");
}

static const char *KindName(int kind)
{
	switch (kind) {
	case BTK_FLYTHROUGH: return "flythrough";
	case BTK_STATIC: return "static";
	case BTK_FOLLOW: return "follow";
	case BTK_STRESS: return "stress";
	case BTK_STREAMING: return "streaming";
	case BTK_TELEPORT: return "teleport";
	}
	return "static";
}

static const struct { uint32_t bit; const char *name; } kCapNames[] = {
	{ BENCH_CAP_GPU_FENCE, "gpu_fence" }, { BENCH_CAP_GPU_SYNC, "gpu_sync" },
	{ BENCH_CAP_VIEWPORT, "viewport" }, { BENCH_CAP_DRAW_MODE, "draw_mode" },
	{ BENCH_CAP_FLAT_TEX, "flat_tex" }, { BENCH_CAP_SECTIONS, "sections" },
	{ BENCH_CAP_GL_COUNTERS, "gl_counters" }, { BENCH_CAP_DISK, "disk" },
	{ BENCH_CAP_THREADS, "threads" }, { BENCH_CAP_CPU_LOAD, "cpu_load" }, { BENCH_CAP_ALLOC, "alloc" },
};

static void JSettings(Buf *b, const char *s)
{
	Put(b, "{");
	bool first = true;
	while (s && *s) {
		const char *end = strchr(s, ';');
		if (!end) end = s + strlen(s);
		const char *eq = s;
		while (eq < end && *eq != '=') eq++;
		char key[64], val[256];
		int kl = (int)(eq - s), vl = eq < end ? (int)(end - eq - 1) : 0;
		while (kl > 0 && s[kl - 1] == ' ') kl--;
		const char *k0 = s;
		while (kl > 0 && *k0 == ' ') { k0++; kl--; }
		if (kl > 0) {
			if (kl >= (int)sizeof(key)) kl = sizeof(key) - 1;
			if (vl >= (int)sizeof(val)) vl = sizeof(val) - 1;
			memcpy(key, k0, kl); key[kl] = 0;
			memcpy(val, eq < end ? eq + 1 : eq, vl); val[vl] = 0;
			if (!first) Put(b, ",");
			first = false;
			JStr(b, key); Put(b, ":"); JStr(b, val);
		}
		s = *end ? end + 1 : end;
	}
	Put(b, "}");
}

static double MiB(uint32_t bytes) { return bytes / 1048576.0; }

static void JTest(Buf *b, int ti)
{
	const BenchRun *r = gRun;
	const BenchTestResult *t = &r->tests[ti];
	const TestExtra *e = &gExtra[ti];
	uint32_t caps = r->caps;
	bool has = t->frameCount > 0;
	bool gl = (caps & BENCH_CAP_GL_COUNTERS) != 0;
	bool disk = (caps & BENCH_CAP_DISK) != 0;
	bool alloc = (caps & BENCH_CAP_ALLOC) != 0;

	Put(b, "  {"); JKey(b, "id"); JStr(b, t->id);
	Put(b, ","); JKey(b, "group"); JStr(b, t->group);
	Put(b, ","); JKey(b, "name"); JStr(b, t->name);
	Put(b, ","); JKey(b, "variant"); JStr(b, t->variant);
	Put(b, ","); JKey(b, "viewpoint"); JStr(b, t->viewpoint);
	Put(b, ","); JKey(b, "kind"); JStr(b, KindName(t->kind));
	Put(b, ","); JKey(b, "skipped"); JBool(b, t->skipped);
	Put(b, ","); JKey(b, "skip_reason"); JStr(b, t->skipReason);
	Put(b, ","); JKey(b, "interrupted"); JBool(b, t->interrupted);
	Put(b, ",\n   "); JKey(b, "scene");
	Put(b, "{\"hour\":"); if (t->hour >= 0) JInt(b, t->hour); else Put(b, "null");
	Put(b, ",\"minute\":"); if (t->minute >= 0) JInt(b, t->minute); else Put(b, "null");
	Put(b, ",\"weather\":"); if (t->weather >= 0) JInt(b, t->weather); else Put(b, "null");
	Put(b, ",\"ped_density\":"); JNum(b, t->pedDensity);
	Put(b, ",\"car_density\":"); JNum(b, t->carDensity);
	Put(b, ",\"fixed_step\":"); JBool(b, t->fixedStep);
	Put(b, ",\"paused\":"); JBool(b, t->paused);
	Put(b, "},\n   ");
	JKey(b, "frames"); JU64(b, t->frameCount);
	Put(b, ","); JKey(b, "first_frame"); JU64(b, has ? t->firstFrame : 0);
	Put(b, ","); JKey(b, "seconds"); JNum(b, t->seconds);
	Put(b, ","); JKey(b, "fps"); JNumOr(b, t->fps, has);
	Put(b, ",\n   "); JStats(b, "frame_ms", &t->frameMs, has);
	Put(b, ",\n   "); JStats(b, "work_ms", &t->workMs, has);
	Put(b, ",\n   "); JStats(b, "swap_ms", &t->swapMs, has);
	Put(b, ",\n   "); JStats(b, "gpu_ms", &t->gpuMs, t->gpuSamples > 0);
	Put(b, ",\n   "); JStats(b, "gpu_sync_ms", &t->gpuSyncMs, e->gpuSyncSamples > 0);
	Put(b, ",\n   "); JKey(b, "gpu_samples"); JU64(b, t->gpuSamples);
	Put(b, ","); JKey(b, "cpu_bound_pct"); JNumOr(b, t->cpuBoundPct, t->gpuSamples > 0);
	Put(b, ","); JKey(b, "gpu_bound_pct"); JNumOr(b, t->gpuBoundPct, t->gpuSamples > 0);
	Put(b, ",\n   "); JKey(b, "groups_ms");
	if (has && (caps & BENCH_CAP_SECTIONS)) {
		Put(b, "{");
		for (int g = 0; g < BG_COUNT; g++) {
			if (g) Put(b, ",");
			JKey(b, gBenchGroupNames[g]); JNum(b, t->groupMs[g]);
		}
		Put(b, "}");
	} else
		Put(b, "null");
	Put(b, ",\n   "); JKey(b, "hitches"); JU64(b, t->hitches);
	Put(b, ","); JKey(b, "worst_hitch_ms"); JNumOr(b, t->worstHitchMs, has);
	Put(b, ","); JKey(b, "worst_hitch_group"); JStr(b, t->worstHitchGroup);
	Put(b, ",\n   "); JKey(b, "per_frame");
	if (has) {
		Put(b, "{\"draws\":"); JNumOr(b, t->draws, gl);
		Put(b, ",\"indices\":"); JNumOr(b, t->indices, gl);
		Put(b, ",\"uniforms\":"); JNumOr(b, t->uniforms, gl);
		Put(b, ",\"tex_binds\":"); JNumOr(b, t->texBinds, gl);
		Put(b, ",\"upload_kib\":"); JNumOr(b, t->uploadKiB, gl);
		Put(b, ",\"disk_kib\":"); JNumOr(b, t->diskKiB, disk);
		Put(b, ",\"peds\":"); JNum(b, t->peds);
		Put(b, ",\"vehicles\":"); JNum(b, t->vehicles);
		Put(b, ",\"objects\":"); JNum(b, t->objects);
		Put(b, "}");
	} else
		Put(b, "null");

	const BenchCounters *c = &t->counters;
	Put(b, ",\n   "); JKey(b, "totals");
	struct { const char *key; uint64_t v; bool present; } tot[] = {
		{ "draws", c->draws, gl }, { "indices", c->indices, gl }, { "uniforms", c->uniforms, gl },
		{ "tex_binds", c->texBinds, gl }, { "buf_binds", c->bufBinds, gl }, { "programs", c->programs, gl },
		{ "state_calls", c->stateCalls, gl }, { "vertex_upload_bytes", c->vertexUploadBytes, gl },
		{ "index_upload_bytes", c->indexUploadBytes, gl }, { "texture_upload_bytes", c->textureUploadBytes, gl },
		{ "texture_upload_ms", 0, gl }, { "shader_compiles", c->shaderCompiles, gl },
		{ "disk_reads", c->diskReads, disk }, { "disk_bytes", c->diskBytes, disk },
		{ "disk_read_ms", 0, disk }, { "disk_sync_waits", c->diskSyncWaits, disk },
		{ "disk_sync_ms", 0, disk }, { "alloc_calls", c->allocCalls, alloc },
		{ "free_calls", c->freeCalls, alloc }, { "alloc_bytes", c->allocBytes, alloc },
	};
	Put(b, "{");
	for (size_t i = 0; i < sizeof(tot) / sizeof(tot[0]); i++) {
		if (i) Put(b, ",");
		JKey(b, tot[i].key);
		if (!tot[i].present) Put(b, "null");
		else if (Eq(tot[i].key, "texture_upload_ms")) JNum(b, c->textureUploadUs / 1000.0);
		else if (Eq(tot[i].key, "disk_read_ms")) JNum(b, c->diskReadUs / 1000.0);
		else if (Eq(tot[i].key, "disk_sync_ms")) JNum(b, c->diskSyncUs / 1000.0);
		else JU64(b, tot[i].v);
	}
	Put(b, "}");

	// thread CPU time per second of the test, matched by name
	Put(b, ",\n   "); JKey(b, "threads"); Put(b, "[");
	double wall = e->wall1 > e->wall0 ? (e->wall1 - e->wall0) / 1e6 : 0;
	if ((caps & BENCH_CAP_THREADS) && wall > 0) {
		bool first = true;
		int ne = t->threadsEnd.count < BENCH_MAX_THREADS ? t->threadsEnd.count : BENCH_MAX_THREADS;
		int ns = t->threadsStart.count < BENCH_MAX_THREADS ? t->threadsStart.count : BENCH_MAX_THREADS;
		for (int i = 0; i < ne; i++) {
			int j = 0;
			while (j < ns && strncmp(t->threadsStart.name[j], t->threadsEnd.name[i], 24) != 0) j++;
			if (j == ns) continue;
			uint64_t a = t->threadsStart.runUs[j], z = t->threadsEnd.runUs[i];
			char name[25];
			memcpy(name, t->threadsEnd.name[i], 24);
			name[24] = 0;
			if (!first) Put(b, ",");
			first = false;
			Put(b, "{\"name\":"); JStr(b, name);
			Put(b, ",\"ms_per_s\":"); JNum(b, z >= a ? (z - a) / 1000.0 / wall : 0);
			Put(b, "}");
		}
	}
	Put(b, "]");

	Put(b, ","); JKey(b, "cpu_load_pct");
	int cores = std::min(t->cpuStart.cores, t->cpuEnd.cores);
	if (cores > 4) cores = 4;
	uint64_t cwall = t->cpuEnd.wallUs > t->cpuStart.wallUs ? t->cpuEnd.wallUs - t->cpuStart.wallUs : 0;
	if ((caps & BENCH_CAP_CPU_LOAD) && cores > 0 && cwall > 0) {
		Put(b, "[");
		for (int i = 0; i < cores; i++) {
			uint64_t idle = t->cpuEnd.idleUs[i] >= t->cpuStart.idleUs[i] ? t->cpuEnd.idleUs[i] - t->cpuStart.idleUs[i] : 0;
			double load = 100.0 * (1.0 - (double)idle / cwall);
			if (i) Put(b, ",");
			JNum(b, load < 0 ? 0 : load > 100 ? 100 : load);
		}
		Put(b, "]");
	} else
		Put(b, "null");

	const BenchMemory *m = &t->memEnd;
	Put(b, ",\n   "); JKey(b, "memory");
	if (m->heapUsed || m->heapTotal || m->gpuRamTotal || m->cdramTotal || m->phycontTotal) {
		Put(b, "{\"heap_used_mib\":"); JNumOr(b, MiB(m->heapUsed), m->heapUsed || m->heapTotal);
		Put(b, ",\"heap_total_mib\":"); JNumOr(b, MiB(m->heapTotal), m->heapTotal != 0);
		Put(b, ",\"gpu_ram_free_mib\":"); JNumOr(b, MiB(m->gpuRamFree), m->gpuRamTotal != 0);
		Put(b, ",\"cdram_free_mib\":"); JNumOr(b, MiB(m->cdramFree), m->cdramTotal != 0);
		Put(b, ",\"phycont_free_mib\":"); JNumOr(b, MiB(m->phycontFree), m->phycontTotal != 0);
		Put(b, "}");
	} else
		Put(b, "null");
	Put(b, ","); JKey(b, "load_ms"); JNumOr(b, t->loadMs, t->loadMs >= 0);
	Put(b, ","); JKey(b, "settle_ms"); JNumOr(b, t->settleMs, t->settleMs >= 0);

	Put(b, ",\n   "); JKey(b, "sections"); Put(b, "[");
	double frames = has ? (double)t->frameCount : 1.0;
	for (int i = 0; i < t->numSections; i++) {
		const BenchSectionStat *s = &t->sections[i];
		Put(b, i ? ",\n    " : "\n    ");
		Put(b, "{\"name\":"); JStr(b, s->name);
		Put(b, ",\"parent\":"); JInt(b, s->parent);
		Put(b, ",\"depth\":"); JInt(b, s->depth);
		Put(b, ",\"ms\":"); JNum(b, s->totalUs / frames / 1000.0);
		Put(b, ",\"self_ms\":"); JNum(b, s->selfUs / frames / 1000.0);
		Put(b, ",\"max_ms\":"); JNum(b, s->maxFrameUs / 1000.0);
		Put(b, ",\"calls_per_frame\":"); JNum(b, s->calls / frames);
		Put(b, "}");
	}
	Put(b, "]}");
}

static void BuildJson(Buf *b, bool partial)
{
	const BenchRun *r = gRun;
	const BenchDeviceInfo *d = &r->device;
	Put(b, "{\n "); JKey(b, "schema"); JStr(b, "relcs-bench/1");
	Put(b, ",\n "); JKey(b, "partial"); JBool(b, partial);
	Put(b, ",\n "); JKey(b, "aborted"); JBool(b, r->aborted);
	Put(b, ",\n "); JKey(b, "timestamp"); JStr(b, r->timestamp);
	Put(b, ",\n "); JKey(b, "device");
	Put(b, "{\"platform\":"); JStr(b, d->platform);
	Put(b, ",\"model\":"); JStr(b, d->model);
	Put(b, ",\"firmware\":"); JStr(b, d->firmware);
	Put(b, ",\"build\":"); JStr(b, d->build);
	Put(b, ",\"cpu_mhz\":"); if (d->cpuMHz > 0) JInt(b, d->cpuMHz); else Put(b, "null");
	Put(b, ",\"bus_mhz\":"); if (d->busMHz > 0) JInt(b, d->busMHz); else Put(b, "null");
	Put(b, ",\"gpu_mhz\":"); if (d->gpuMHz > 0) JInt(b, d->gpuMHz); else Put(b, "null");
	Put(b, ",\"xbar_mhz\":"); if (d->xbarMHz > 0) JInt(b, d->xbarMHz); else Put(b, "null");
	Put(b, ",\"gpu\":"); JStr(b, d->gpu);
	Put(b, ",\"screen\":["); JInt(b, d->screenW); Put(b, ","); JInt(b, d->screenH);
	Put(b, "],\"notes\":"); JStr(b, d->notes);
	Put(b, "},\n "); JKey(b, "caps"); Put(b, "[");
	bool first = true;
	for (size_t i = 0; i < sizeof(kCapNames) / sizeof(kCapNames[0]); i++)
		if (r->caps & kCapNames[i].bit) {
			if (!first) Put(b, ",");
			first = false;
			JStr(b, kCapNames[i].name);
		}
	Put(b, "],\n "); JKey(b, "settings"); JSettings(b, r->settings);
	Put(b, ",\n "); JKey(b, "boot");
	Put(b, "{\"engine_ms\":"); JNumOr(b, r->bootEngineMs, r->bootEngineMs >= 0);
	Put(b, ",\"game_ms\":"); JNumOr(b, r->bootGameMs, r->bootGameMs >= 0);
	Put(b, ",\"ready_ms\":"); JNumOr(b, r->bootReadyMs, r->bootReadyMs >= 0);
	Put(b, "},\n "); JKey(b, "scores");
	Put(b, "{\"graphics\":"); JNumOr(b, r->scores.graphics, r->scores.graphics > 0);
	Put(b, ",\"cpu\":"); JNumOr(b, r->scores.cpu, r->scores.cpu > 0);
	Put(b, ",\"overall\":"); JNumOr(b, r->scores.overall, r->scores.overall > 0);
	Put(b, ",\"valid\":"); JBool(b, r->scores.valid);
	Put(b, "},\n "); JKey(b, "tests"); Put(b, "[");
	first = true;
	for (int i = 0; i < r->numTests; i++) {
		if (!TestDone(i)) continue;	// the test in progress
		Put(b, first ? "\n" : ",\n");
		first = false;
		JTest(b, i);
	}
	Put(b, "],\n "); JKey(b, "deltas"); Put(b, "[");
	for (int i = 0; i < r->numDeltas; i++) {
		const BenchDelta *x = &r->deltas[i];
		Put(b, i ? ",\n  " : "\n  ");
		Put(b, "{\"viewpoint\":"); JStr(b, x->viewpoint);
		Put(b, ",\"variant\":"); JStr(b, x->variant);
		Put(b, ",\"reference\":"); JStr(b, DeltaReference(x->variant));
		Put(b, ",\"baseline_ms\":"); JNum(b, x->baselineMs);
		Put(b, ",\"variant_ms\":"); JNum(b, x->variantMs);
		Put(b, ",\"saved_ms\":"); JNum(b, x->savedMs);
		Put(b, ",\"saved_pct\":"); JNum(b, x->savedPct);
		Put(b, ",\"baseline_gpu_ms\":"); JNumOr(b, x->baselineGpuMs, x->baselineGpuMs >= 0);
		Put(b, ",\"variant_gpu_ms\":"); JNumOr(b, x->variantGpuMs, x->variantGpuMs >= 0);
		Put(b, ",\"baseline_work_ms\":"); JNum(b, x->baselineWorkMs);
		Put(b, ",\"variant_work_ms\":"); JNum(b, x->variantWorkMs);
		Put(b, "}");
	}
	Put(b, "],\n "); JKey(b, "targets"); Put(b, "[");
	for (int i = 0; i < r->numTargets; i++) {
		const BenchTarget *x = &r->targets[i];
		Put(b, i ? ",\n  " : "\n  ");
		Put(b, "{\"what\":"); JStr(b, x->what);
		Put(b, ",\"kind\":"); JStr(b, x->kind);
		Put(b, ",\"ms\":"); JNum(b, x->ms);
		Put(b, ",\"pct\":"); JNum(b, x->pct);
		Put(b, ",\"advice\":"); JStr(b, gAdvice[i][0] ? gAdvice[i] : x->advice);
		Put(b, "}");
	}
	Put(b, "]\n}\n");
}

// ---- frames.csv ----------------------------------------------------------------

static void Ms3(char *out, int size, uint32_t us)	// exact "123.456"
{
	snprintf(out, size, "%u.%03u", (unsigned)(us / 1000), (unsigned)(us % 1000));
}

static void BuildCsv(Buf *b)
{
	const BenchRun *r = gRun;
	Put(b, BENCH_CSV_HEADER "\n");
	for (uint32_t i = 0; i < r->numFrames; i++) {
		const BenchFrame *f = &r->frames[i];
		const char *id = f->test < r->numTests ? r->tests[f->test].id : "";
		char c[14][24];
		Ms3(c[0], 24, f->tUs); Ms3(c[1], 24, f->frameUs); Ms3(c[2], 24, f->workUs);
		Ms3(c[3], 24, f->swapUs); Ms3(c[4], 24, f->gpuUs); Ms3(c[5], 24, f->gpuSyncUs);
		for (int g = 0; g < BG_COUNT; g++) Ms3(c[6 + g], 24, f->groupUs[g]);
		// ids are plain identifiers; quote anything else
		if (strpbrk(id, ",\"\n")) {
			Put(b, "\"");
			for (const char *p = id; *p; p++) {
				if (*p == '"') Put(b, "\"\"");
				else PutN(b, p, 1);
			}
			Put(b, "\"");
		} else
			Put(b, id);
		Putf(b, ",%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%u,%u,%u,%.3f,%.3f,%u,%u,%u,%u,%.3f,%.3f,%.3f,%u\n",
		     (unsigned)f->index, c[0], c[1], c[2], c[3], c[4], c[5],
		     c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13],
		     (unsigned)f->draws, (unsigned)f->indices, (unsigned)f->uniforms, (unsigned)f->texBinds,
		     f->uploadBytes / 1024.0, f->diskBytes / 1024.0,
		     (unsigned)f->peds, (unsigned)f->vehicles, (unsigned)f->objects, (unsigned)f->streamRequests,
		     Finite(f->camX) ? f->camX : 0.0, Finite(f->camY) ? f->camY : 0.0, Finite(f->camZ) ? f->camZ : 0.0,
		     (unsigned)f->flags);
	}
}

// ---- summary.txt ---------------------------------------------------------------

static void Num(char *out, int size, double v, int decimals, bool present)
{
	if (!present || !Finite(v)) snprintf(out, size, "-");
	else snprintf(out, size, "%.*f", decimals, v);
}

// "\nTitle\n-----\n", underline as long as the title in characters
static void Heading(Buf *b, const char *title, char under)
{
	if (b->len) Put(b, "\n");
	Put(b, title);
	Put(b, "\n");
	int chars = 0;
	for (const unsigned char *p = (const unsigned char *)title; *p; p++)
		if ((*p & 0xC0) != 0x80) chars++;
	for (int i = 0; i < chars; i++) PutN(b, &under, 1);
	Put(b, "\n");
}

static const char *GroupTitle(const char *group)
{
	if (Eq(group, "graphics")) return "Graphismes";
	if (Eq(group, "cpu")) return "CPU";
	if (Eq(group, "streaming")) return "Streaming";
	if (Eq(group, "isolation")) return "Isolation";
	if (Eq(group, "boot")) return "D" FR_E "marrage";
	return group;
}

static void Status(char *out, int size, const BenchTestResult *t)
{
	if (t->skipped) snprintf(out, size, "ignor" FR_E " : %s", t->skipReason[0] ? t->skipReason : "?");
	else if (t->interrupted) snprintf(out, size, "interrompu");
	else if (!t->frameCount) snprintf(out, size, "aucune image");
	else snprintf(out, size, "OK");
}

static void SummaryHeader(Buf *b)
{
	const BenchRun *r = gRun;
	const BenchDeviceInfo *d = &r->device;
	Heading(b, "reLCS Benchmark - R" FR_E "sultats", '=');
	Put(b, "\n");
	Putf(b, "Date        : %s\n", r->timestamp);
	Putf(b, "Appareil    : %s", d->platform);
	if (d->model[0]) Putf(b, " - %s", d->model);
	Put(b, "\n");
	Putf(b, "Firmware    : %s\n", d->firmware[0] ? d->firmware : "inconnu");
	Putf(b, "Build       : %s\n", d->build[0] ? d->build : "inconnu");
	char cpu[16], bus[16], gpu[16], xbar[16];
	Num(cpu, sizeof(cpu), d->cpuMHz, 0, d->cpuMHz > 0);
	Num(bus, sizeof(bus), d->busMHz, 0, d->busMHz > 0);
	Num(gpu, sizeof(gpu), d->gpuMHz, 0, d->gpuMHz > 0);
	Num(xbar, sizeof(xbar), d->xbarMHz, 0, d->xbarMHz > 0);
	if (d->cpuMHz > 0 || d->busMHz > 0 || d->gpuMHz > 0 || d->xbarMHz > 0)
		Putf(b, "Horloges    : CPU %s MHz, bus %s MHz, GPU %s MHz, xbar %s MHz\n", cpu, bus, gpu, xbar);
	else
		Put(b, "Horloges    : inconnues\n");
	Putf(b, "GPU         : %s, " FR_E "cran %dx%d\n", d->gpu[0] ? d->gpu : "inconnu", d->screenW, d->screenH);
	Putf(b, "R" FR_E "glages    : %s\n", r->settings[0] ? r->settings : "par d" FR_E "faut");
	Put(b, "Capacit" FR_E "s   : ");
	bool first = true;
	for (size_t i = 0; i < sizeof(kCapNames) / sizeof(kCapNames[0]); i++)
		if (r->caps & kCapNames[i].bit) {
			Putf(b, "%s%s", first ? "" : ", ", kCapNames[i].name);
			first = false;
		}
	Put(b, first ? "aucune\n" : "\n");
	if (d->notes[0]) Putf(b, "Notes       : %s\n", d->notes);
	if (r->aborted) Put(b, "\nATTENTION : s" FR_E "rie interrompue avant la fin (r" FR_E "sultats partiels).\n");
	if (gDropped) Putf(b, "\nATTENTION : %u images non enregistr" FR_E "es (tampon plein).\n", (unsigned)gDropped);
}

static void SummaryScores(Buf *b)
{
	const BenchScores *s = &gRun->scores;
	char g[16], c[16], o[16];
	Num(g, sizeof(g), s->graphics, 0, s->graphics > 0);
	Num(c, sizeof(c), s->cpu, 0, s->cpu > 0);
	Num(o, sizeof(o), s->overall, 0, s->overall > 0);
	Heading(b, "Scores", '-');
	Putf(b, "Graphismes : %s\n", g);
	Putf(b, "CPU        : %s\n", c);
	Putf(b, "Global     : %s\n", o);
	if (!s->valid)
		Put(b, "(scores non valides : il manque des tests graphiques ou CPU termin" FR_E "s)\n");
}

static void SummaryTests(Buf *b)
{
	const BenchRun *r = gRun;
	static const char *const groups[] = { "graphics", "cpu", "streaming", "isolation", NULL };
	Heading(b, "Tests", '-');
	Putf(b, "%-38s%9s%9s%9s%9s%9s  %s  %s\n", "Test", "FPS moy.", "1 % bas", "p95 ms", "CPU ms", "GPU ms",
	     "% limit" FR_E " CPU/GPU", "Statut");
	for (int pass = 0; pass < 5; pass++) {
		bool header = false;
		for (int i = 0; i < r->numTests; i++) {
			const BenchTestResult *t = &r->tests[i];
			if (!TestDone(i)) continue;
			bool known = false;
			for (int k = 0; groups[k]; k++) if (Eq(t->group, groups[k])) known = true;
			if (pass < 4 ? !Eq(t->group, groups[pass]) : known) continue;
			if (!header) {
				Putf(b, "[%s]\n", pass < 4 ? GroupTitle(groups[pass]) : "Autres");
				header = true;
			}
			bool has = t->frameCount > 0;
			char fps[16], low[16], p95[16], cpu[16], gpu[16], bound[24], status[96];
			Num(fps, sizeof(fps), t->fps, 2, has);
			Num(low, sizeof(low), t->frameMs.low1Fps, 2, has);
			Num(p95, sizeof(p95), t->frameMs.p95, 2, has);
			Num(cpu, sizeof(cpu), t->workMs.mean, 2, has);
			Num(gpu, sizeof(gpu), t->gpuMs.mean, 2, t->gpuSamples > 0);
			if (t->gpuSamples) snprintf(bound, sizeof(bound), "%3.0f / %-3.0f", t->cpuBoundPct, t->gpuBoundPct);
			else snprintf(bound, sizeof(bound), "-");
			Status(status, sizeof(status), t);
			Putf(b, "%-38s%9s%9s%9s%9s%9s  %-16s  %s\n", t->id, fps, low, p95, cpu, gpu, bound, status);
		}
	}
}

static void SummaryTargets(Buf *b)
{
	const BenchRun *r = gRun;
	Heading(b, FR_AA " optimiser en priorit" FR_E, '-');
	if (!r->numTargets) Put(b, "Rien de significatif d" FR_E "tect" FR_E ".\n");
	for (int i = 0; i < r->numTargets; i++) {
		const BenchTarget *t = &r->targets[i];
		if (Eq(t->kind, "streaming"))
			Putf(b, "%2d. %s [%s] : %.0f ms", i + 1, t->what, t->kind, t->ms);
		else
			Putf(b, "%2d. %s [%s] : %.2f ms par image (%.1f %%)", i + 1, t->what, t->kind, t->ms, t->pct);
		Putf(b, "\n    -> %s\n", gAdvice[i][0] ? gAdvice[i] : t->advice);
	}
}

static void SummaryIsolation(Buf *b)
{
	const BenchRun *r = gRun;
	bool any = false;
	for (int i = 0; i < r->numTests; i++)
		if (TestDone(i) && r->tests[i].viewpoint[0]) any = true;
	if (!any) return;
	Heading(b, "Isolation (gain = r" FR_E "f" FR_E "rence - variante, positif = co" FR_UC "t de la fonction)", '-');
	char seen[16][32];
	int numSeen = 0;
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *vt = &r->tests[i];
		if (!TestDone(i) || !vt->viewpoint[0]) continue;
		bool done = false;
		for (int k = 0; k < numSeen; k++) if (Eq(seen[k], vt->viewpoint)) done = true;
		if (done || numSeen >= 16) continue;
		Copy(seen[numSeen++], 32, vt->viewpoint);
		const char *vp = vt->viewpoint;
		int base = -1;
		for (int j = 0; j < r->numTests; j++)
			if (Usable(j) && Eq(r->tests[j].viewpoint, vp) && Eq(r->tests[j].variant, "baseline")) { base = j; break; }
		if (base >= 0) {
			const BenchTestResult *bt = &r->tests[base];
			char gpu[32];
			if (bt->gpuSamples) snprintf(gpu, sizeof(gpu), "GPU %.2f ms", bt->gpuMs.mean);
			else snprintf(gpu, sizeof(gpu), "GPU non mesur" FR_E);
			Putf(b, "Point de vue %s : r" FR_E "f" FR_E "rence %.2f ms (CPU %.2f ms, %s)\n", vp,
			     bt->frameMs.mean, bt->workMs.mean, gpu);
		} else
			Putf(b, "Point de vue %s : pas de r" FR_E "f" FR_E "rence mesur" FR_E "e\n", vp);
		Putf(b, "  %-22s %-12s %9s %9s %8s %9s  %s\n", "variante", "compar" FR_E "e " FR_A, "ms", "gain ms", "gain %", "GPU ms", "description");
		// deltas of this viewpoint, sorted by gain
		int order[BENCH_MAX_DELTAS], n = 0;
		for (int d = 0; d < r->numDeltas; d++)
			if (Eq(r->deltas[d].viewpoint, vp)) order[n++] = d;
		for (int x = 1; x < n; x++) {
			int v = order[x], y = x - 1;
			while (y >= 0 && r->deltas[order[y]].savedMs < r->deltas[v].savedMs) { order[y + 1] = order[y]; y--; }
			order[y + 1] = v;
		}
		for (int x = 0; x < n; x++) {
			const BenchDelta *d = &r->deltas[order[x]];
			int vi = FindVariant(d->variant);
			char gpu[16];
			Num(gpu, sizeof(gpu), d->variantGpuMs, 2, d->variantGpuMs >= 0);
			Putf(b, "  %-22s %-12s %9.2f %+9.2f %+8.1f %9s  %s\n", d->variant, DeltaReference(d->variant),
			     d->variantMs, d->savedMs, d->savedPct, gpu, vi >= 0 ? kVariants[vi].label : "");
		}
		for (int j = 0; j < r->numTests; j++) {
			const BenchTestResult *t = &r->tests[j];
			if (TestDone(j) && Eq(t->viewpoint, vp) && t->skipped)
				Putf(b, "  %-22s ignor" FR_E " : %s\n", t->variant, t->skipReason[0] ? t->skipReason : "?");
		}
	}
}

static void SummaryStreaming(Buf *b)
{
	const BenchRun *r = gRun;
	bool header = false;
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *t = &r->tests[i];
		if (!TestDone(i) || (t->kind != BTK_STREAMING && t->kind != BTK_TELEPORT)) continue;
		if (!header) {
			Heading(b, "Streaming et t" FR_E "l" FR_E "portations", '-');
			header = true;
		}
		char load[16], settle[16], fps[16];
		Num(load, sizeof(load), t->loadMs, 0, t->loadMs >= 0);
		Num(settle, sizeof(settle), t->settleMs, 0, t->settleMs >= 0);
		Num(fps, sizeof(fps), t->fps, 2, t->frameCount > 0);
		if (t->skipped)
			Putf(b, "%-24s ignor" FR_E " : %s\n", t->id, t->skipReason);
		else if (t->kind == BTK_TELEPORT)
			Putf(b, "%-24s chargement %s ms, stabilisation %s ms, %u images, %s FPS\n", t->id, load, settle,
			     (unsigned)t->frameCount, fps);
		else {
			Putf(b, "%-24s %s FPS, %u pic(s)", t->id, fps, (unsigned)t->hitches);
			if (t->hitches)
				Putf(b, ", pire %.0f ms (groupe %s)", t->worstHitchMs, t->worstHitchGroup[0] ? t->worstHitchGroup : "-");
			if (t->loadMs >= 0) Putf(b, ", chargement %s ms", load);
			Put(b, "\n");
		}
	}
}

static void SummaryHitches(Buf *b)
{
	const BenchRun *r = gRun;
	Heading(b, "Pics (images > max(100 ms, 3 x m" FR_E "diane))", '-');
	bool any = false;
	for (int i = 0; i < r->numTests; i++) {
		const BenchTestResult *t = &r->tests[i];
		if (!TestDone(i) || !t->hitches) continue;
		any = true;
		Putf(b, "%-38s %u pic(s), pire %.0f ms (groupe %s)\n", t->id, (unsigned)t->hitches, t->worstHitchMs,
		     t->worstHitchGroup[0] ? t->worstHitchGroup : "-");
	}
	if (!any) Put(b, "Aucun.\n");
}

static void SummaryBoot(Buf *b)
{
	const BenchRun *r = gRun;
	char e[16], g[16], y[16];
	Num(e, sizeof(e), r->bootEngineMs, 0, r->bootEngineMs >= 0);
	Num(g, sizeof(g), r->bootGameMs, 0, r->bootGameMs >= 0);
	Num(y, sizeof(y), r->bootReadyMs, 0, r->bootReadyMs >= 0);
	Heading(b, "D" FR_E "marrage (depuis le lancement)", '-');
	Putf(b, "Moteur %s ms, jeu %s ms, pr" FR_EC "t %s ms\n", e, g, y);
}

static void SummaryHelp(Buf *b)
{
	Heading(b, "Fichiers", '-');
	Put(b, "results.json : toutes les mesures (sch" FR_E "ma relcs-bench/1, lu par tools/vita/bench-report.py)\n");
	Put(b, "frames.csv   : une ligne par image mesur" FR_E "e\n");
	Put(b, "summary.txt  : ce r" FR_E "sum" FR_E "\n");
	Put(b, "log.txt      : (Vita) journal du moteur, une fen" FR_EC "tre [PERF] par test\n");
	Heading(b, "Comment lire ces r" FR_E "sultats", '-');
	Put(b, "- FPS moy. : images par seconde (plus haut = mieux). 1 % bas : FPS des 1 % d'images les plus lentes (saccades).\n");
	Put(b, "- CPU ms : travail du thread principal jusqu'" FR_A " l'envoi de l'image. GPU ms : temps GPU par image (- = non mesur" FR_E ").\n");
	Put(b, "- % limit" FR_E " CPU/GPU : comparaison des dur" FR_E "es sur les images avec mesure GPU (- = inconnu).\n");
	Put(b, "- Isolation : chaque variante d" FR_E "sactive une seule fonction sur la m" FR_EC "me image fig" FR_E "e ; un gain positif est le co" FR_UC "t de la fonction.\n");
	Put(b, "  sim_running (simulation relanc" FR_E "e) donne un gain n" FR_E "gatif : le co" FR_UC "t de la simulation ; no_audio est compar" FR_E " " FR_A " sim_running.\n");
	Put(b, "- viewport_50 : s'il gagne plus de 25 % de l'image, le GPU est limit" FR_E " par le remplissage ; sinon par la g" FR_E "om" FR_E "trie et les " FR_E "tats.\n");
	Put(b, "- Scores : 100 x moyenne g" FR_E "om" FR_E "trique des FPS (graphismes, CPU ; global = graphismes + CPU + s1). Comparer la m" FR_EC "me version dans le m" FR_EC "me mode.\n");
	Put(b, "- Les tests ignor" FR_E "s ou interrompus sont exclus des scores.\n");
}

static void BuildSummary(Buf *b)
{
	SummaryHeader(b);
	SummaryScores(b);
	SummaryTests(b);
	SummaryBoot(b);
	SummaryTargets(b);
	SummaryIsolation(b);
	SummaryStreaming(b);
	SummaryHitches(b);
	SummaryHelp(b);
}

// ---- files ---------------------------------------------------------------------

// Creates every level of the result folder (the drive/root component excepted).
static bool MakeFolder(void)
{
	if (gFolderReady) return true;
	char path[sizeof(gRun->folder)];
	Copy(path, sizeof(path), gRun->folder);
	int len = (int)strlen(path);
	while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) path[--len] = 0;
	if (!len) return false;
	bool ok = false;
	bool first = true;
	for (int i = 1; i <= len; i++) {
		if (i < len && path[i] != '/' && path[i] != '\\') continue;
		char save = path[i];
		path[i] = 0;
		bool root = first && (strchr(path, ':') != NULL || path[0] == 0);
		first = false;
		if (!root) ok = BenchPlatform::MakeDir(path);
		path[i] = save;
	}
	gFolderReady = ok;
	return ok;
}

static bool WriteBuf(const char *name, const Buf *b)
{
	if (b->fail || !b->p) return false;
	char path[sizeof(gRun->folder) + 32];
	size_t n = strlen(gRun->folder);
	bool slash = n && (gRun->folder[n - 1] == '/' || gRun->folder[n - 1] == '\\');
	snprintf(path, sizeof(path), "%s%s%s", gRun->folder, slash ? "" : "/", name);
	return BenchPlatform::WriteFile(path, b->p, b->len);
}

} // namespace

// ---- public API ----------------------------------------------------------------

namespace BenchMetrics {

bool Init(BenchRun *run, uint32_t maxFrames)
{
	if (!run) return false;
	if (maxFrames < 1) maxFrames = 1;
	if (maxFrames > 0x7FFFFFFFu / sizeof(BenchFrame)) return false;
	free(gTestsAlloc); free(gFramesAlloc); free(gExtra); free(gValues); free(gScratch);
	gTestsAlloc = (BenchTestResult *)calloc(BENCH_MAX_TESTS, sizeof(BenchTestResult));
	gFramesAlloc = (BenchFrame *)malloc(maxFrames * sizeof(BenchFrame));
	gExtra = (TestExtra *)calloc(BENCH_MAX_TESTS, sizeof(TestExtra));
	gValues = (double *)malloc(maxFrames * sizeof(double));
	gScratch = (double *)malloc(maxFrames * sizeof(double));
	gRun = NULL;
	if (!gTestsAlloc || !gFramesAlloc || !gExtra || !gValues || !gScratch) {
		free(gTestsAlloc); free(gFramesAlloc); free(gExtra); free(gValues); free(gScratch);
		gTestsAlloc = NULL; gFramesAlloc = NULL; gExtra = NULL; gValues = gScratch = NULL;
		return false;
	}
	run->tests = gTestsAlloc;
	run->numTests = 0;
	run->frames = gFramesAlloc;
	run->numFrames = 0;
	run->maxFrames = maxFrames;
	run->numDeltas = 0;
	run->numTargets = 0;
	memset(&run->scores, 0, sizeof(run->scores));
	if (!run->timestamp[0]) BenchPlatform::Timestamp(run->timestamp, sizeof(run->timestamp));
	if (!run->folder[0]) {
		const char *root = BenchPlatform::ResultsRoot();
		int n = snprintf(run->folder, sizeof(run->folder), "%s%s/", root ? root : "", run->timestamp);
		if (n < 0 || n >= (int)sizeof(run->folder)) run->folder[0] = 0;	// too long: writes fail
	}
	if (!run->device.platform[0]) {
		memset(&run->device, 0, sizeof(run->device));
		BenchPlatform::DeviceInfo(&run->device);
	}
	if (!run->caps) run->caps = BenchPlatform::Caps();

	gCur = -1;
	gMeasuring = false;
	gNextFrameId = 0;
	gDropped = 0;
	gFolderReady = false;
	memset(&gF, 0, sizeof(gF));
	memset(gRing, 0, sizeof(gRing));
	memset(gSecMap, 0xFF, sizeof(gSecMap));
	memset(gSecGroup, 0xFE, sizeof(gSecGroup));
	memset(gAdvice, 0, sizeof(gAdvice));
	gRun = run;
	return true;
}

BenchRun *Run(void) { return gRun; }

BenchTestResult *BeginTest(const char *id, const char *group, const char *name, int kind)
{
	if (!gRun) return NULL;
	if (gCur >= 0) EndTest();
	if (gRun->numTests >= BENCH_MAX_TESTS) return NULL;	// the director stops and writes
	int i = gRun->numTests++;
	BenchTestResult *t = &gRun->tests[i];
	memset(t, 0, sizeof(*t));
	Copy(t->id, sizeof(t->id), id);
	Copy(t->group, sizeof(t->group), group);
	Copy(t->name, sizeof(t->name), name);
	t->kind = kind;
	t->hour = t->minute = t->weather = -1;
	t->pedDensity = t->carDensity = 1.0f;
	t->loadMs = t->settleMs = -1;
	TestExtra *e = &gExtra[i];
	memset(e, 0, sizeof(*e));
	SampleCounters(&e->base);
	if (gRun->caps & BENCH_CAP_THREADS) BenchPlatform::ReadThreadTimes(&t->threadsStart);
	if (gRun->caps & BENCH_CAP_CPU_LOAD) BenchPlatform::ReadCpuLoad(&t->cpuStart);
	e->wall0 = BenchPlatform::NowUs();
	memset(gSecMap, 0xFF, sizeof(gSecMap));
	gCur = i;
	return t;
}

void StartMeasuring(void)
{
	if (!gRun || gCur < 0 || gMeasuring) return;
	gMeasuring = true;
	TestExtra *e = &gExtra[gCur];
	// totals cover the measured window; a teleport keeps its load in them
	if (!e->rebased) {
		e->rebased = true;
		if (gRun->tests[gCur].kind != BTK_TELEPORT) SampleCounters(&e->base);
	}
}

void StopMeasuring(void)
{
	if (!gRun || !gMeasuring) return;
	if (gF.open && gF.record) {
		if (gF.ended) {
			BenchCounters c;
			SampleCounters(&c);
			DrainGpu();
			CloseFrame(BenchPlatform::NowUs(), &c);
		} else
			gF.record = false;	// stopped mid-frame: that frame is incomplete
	}
	gMeasuring = false;
}

bool Measuring(void) { return gMeasuring; }

void EndTest(void)
{
	if (!gRun || gCur < 0) return;
	StopMeasuring();
	int i = gCur;
	BenchTestResult *t = &gRun->tests[i];
	TestExtra *e = &gExtra[i];
	BenchCounters now;
	SampleCounters(&now);
	CounterDelta(&e->base, &now, &t->counters);
	if (gRun->caps & BENCH_CAP_THREADS) BenchPlatform::ReadThreadTimes(&t->threadsEnd);
	if (gRun->caps & BENCH_CAP_CPU_LOAD) BenchPlatform::ReadCpuLoad(&t->cpuEnd);
	e->wall1 = BenchPlatform::NowUs();
	BenchPlatform::ReadMemory(&t->memEnd);
	DrainGpu();
	ComputeTestStats(i);
	e->ended = true;
	e->dirty = false;
	gCur = -1;
}

void SkipTest(BenchTestResult *t, const char *reason)
{
	if (!t) return;
	t->skipped = true;
	Copy(t->skipReason, sizeof(t->skipReason), reason);
	if (gRun && gCur >= 0 && t == &gRun->tests[gCur]) EndTest();
}

void FrameBegin(uint64_t nowUs)
{
	if (!gRun) return;
	BenchCounters c;
	SampleCounters(&c);
	DrainGpu();
	if (gF.open) CloseFrame(nowUs, &c);
	memset(&gF, 0, sizeof(gF));
	gF.open = true;
	gF.id = gNextFrameId++;
	gF.beginUs = nowUs;
	gF.counters = c;
	gF.record = gMeasuring && gCur >= 0 && gRun->numFrames < gRun->maxFrames;
	if (gMeasuring && gCur >= 0 && !gF.record)
		gDropped++;	// buffer full: no sections either for this frame
	GpuSlot *s = &gRing[gF.id % GPU_RING];
	memset(s, 0, sizeof(*s));
	s->used = true;
	s->frame = gF.id;
	s->record = -1;
}

void SwapBegin(uint64_t nowUs)
{
	if (!gRun || !gF.open || gF.ended || gF.inSwap) return;
	gF.inSwap = true;
	gF.swapStartUs = nowUs;
	if (!gF.hasSwap) {
		gF.hasSwap = true;
		gF.firstSwapUs = nowUs;
	}
}

void SwapEnd(uint64_t nowUs)
{
	if (!gRun || !gF.open || !gF.inSwap) return;
	gF.inSwap = false;
	gF.swapUs += nowUs > gF.swapStartUs ? nowUs - gF.swapStartUs : 0;
	gF.swaps++;
	GpuSlot *s = &gRing[gF.id % GPU_RING];
	if (s->used && s->frame == gF.id && !s->hasSwap) {
		s->hasSwap = true;
		s->swapBeginUs = gF.firstSwapUs;
	}
	BenchPlatform::FrameSubmitted(gF.id);	// blocks in gpu-sync mode
	gF.gpuSyncUs += BenchPlatform::LastGpuSyncWaitUs();
}

void SetFrameContext(float camX, float camY, float camZ, int peds, int vehicles, int objects,
                     int streamRequests, bool paused, bool gpuSync)
{
	if (!gF.open) return;
	gF.cam[0] = camX;
	gF.cam[1] = camY;
	gF.cam[2] = camZ;
	gF.peds = peds;
	gF.vehicles = vehicles;
	gF.objects = objects;
	gF.streamRequests = streamRequests;
	gF.paused = paused;
	gF.gpuSync = gpuSync;
}

void FrameEnd(uint64_t nowUs)
{
	if (!gRun || !gF.open || gF.ended) return;
	gF.ended = true;
	gF.endUs = nowUs;
	if (gF.record && gCur >= 0 && (gRun->caps & BENCH_CAP_SECTIONS))
		ReadSections(&gRun->tests[gCur]);
	DrainGpu();
}

void Analyse(void)
{
	if (!gRun) return;
	if (gCur >= 0) EndTest();
	AnalyseAll();
}

bool WriteResults(void)
{
	if (!gRun) return false;
	AnalyseAll();
	if (!MakeFolder()) return false;
	bool ok = true;
	Buf b;
	memset(&b, 0, sizeof(b));
	BuildJson(&b, false);
	ok &= WriteBuf("results.json", &b);
	b.len = 0;
	b.fail = false;
	Grow(&b, gRun->numFrames * 160u + 1024u);
	BuildCsv(&b);
	ok &= WriteBuf("frames.csv", &b);
	b.len = 0;
	b.fail = false;
	BuildSummary(&b);
	ok &= WriteBuf("summary.txt", &b);
	free(b.p);
	return ok;
}

bool WritePartial(void)
{
	if (!gRun) return false;
	AnalyseAll();
	if (!MakeFolder()) return false;
	Buf b;
	memset(&b, 0, sizeof(b));
	BuildJson(&b, true);
	bool ok = WriteBuf("results.json", &b);
	free(b.p);
	return ok;
}

void ComputeStats(const double *values, int n, double *scratch, BenchStats *out)
{
	if (!out) return;
	memset(out, 0, sizeof(*out));
	if (!values || !scratch || n <= 0) return;
	double sum = 0;
	for (int i = 0; i < n; i++) {
		scratch[i] = values[i];
		sum += values[i];
	}
	std::sort(scratch, scratch + n);
	out->mean = sum / n;
	out->median = scratch[Rank(50, n)];
	out->p90 = scratch[Rank(90, n)];
	out->p95 = scratch[Rank(95, n)];
	out->p99 = scratch[Rank(99, n)];
	out->min = scratch[0];
	out->max = scratch[n - 1];
	int k = n / 100;
	if (k < 1) k = 1;
	double slow = 0;
	for (int i = n - k; i < n; i++) slow += scratch[i];
	slow /= k;
	out->low1Fps = slow > 0 ? 1000.0 / slow : 0;
}

} // namespace BenchMetrics

#endif
