#pragma once

// Statistics behind the PS Vita measurement reports (vita_perf.cpp). No Vita
// or game headers: tools/vita/check-perf-stats.py tests this file on the host.
#include <stdint.h>

// Summary of one report window of samples (frame intervals, CPU work, latency).
struct VitaSampleSummary {
	unsigned count;			// every sample, stored or not
	double meanMs, medianMs, p95Ms, p99Ms, minMs, maxMs;
	unsigned over33, over50;	// samples above 33.3 / 50 ms
	unsigned fpsWindows;		// complete one-second windows
	float fpsMin, fpsMax;		// frames per second over those windows, -1 without any
};

class VitaSampleSeries {
public:
	// 34 s at 60 fps: a 5 s window never fills it. Beyond, mean/min/max and the
	// thresholds still cover every sample; percentiles cover the stored ones.
	enum { CAPACITY = 2048 };
	void Reset(void);
	// One sample in microseconds. For frame intervals, consecutive samples also
	// form one-second windows (frames / duration once a window reaches 1 s).
	void Add(uint32_t us);
	unsigned Count(void) const { return count; }
	unsigned Dropped(void) const { return count > CAPACITY ? count - CAPACITY : 0; }
	// Nearest-rank percentiles on a sorted copy in scratch (CAPACITY entries).
	void Summarise(VitaSampleSummary *out, uint32_t *scratch) const;
private:
	uint32_t samples[CAPACITY];
	unsigned count;
	uint64_t total;
	uint32_t minUs, maxUs;
	unsigned over33, over50;
	uint64_t windowUs;
	unsigned windowFrames, fpsWindows;
	float fpsMin, fpsMax;
};

// CPU frame limiter with a fixed cadence: a frame is due one period after the
// previous deadline, so sleep rounding and scheduler latency don't accumulate.
// A frame more than one period late restarts the schedule from that frame,
// instead of running frames back to back to catch up.
class VitaFramePacer {
public:
	void SetPeriod(uint64_t us) { period = us; next = 0; }
	uint64_t Period(void) const { return period; }
	// 0 when the frame may start at `now` (the next one is then scheduled),
	// otherwise the microseconds left before it is due.
	uint64_t Remaining(uint64_t now);
private:
	uint64_t period, next;
};

struct VitaHoldHistogram;

// Pairs display flips with the swaps that queued them: each swap queues one
// display entry, so flip k shows submit k. A submit's kind is known only when
// its Idle ends (a loading screen inside Idle is not a gameplay frame), which
// is always before its flip is processed on the main thread.
class VitaFlipMatcher {
public:
	enum { PENDING, GAME, OTHER };
	enum { RING = 64 };	// far more than the frames vitaGL keeps in flight
	uint32_t Submit(uint64_t time, uint8_t kind);
	void SetKind(uint32_t index, uint8_t kind) { submits[index % RING].kind = kind; }
	uint32_t Submitted(void) const { return count; }
	// Flips in order: the previous image's hold goes in `holds` if it was a
	// gameplay frame; a gameplay image adds its submit-to-flip latency.
	// Returns true for a gameplay image.
	bool Flip(uint32_t flipIndex, int vcount, uint64_t time, VitaHoldHistogram *holds, VitaSampleSeries *latencies);
	// Callbacks without a submit (ignored; next callback pairs with next submit).
	unsigned Unmatched(void) const { return unmatched; }
private:
	struct { uint64_t time; uint8_t kind; } submits[RING];
	uint32_t count;
	uint32_t offset;	// submit index = flip index - offset
	bool havePrev;
	int prevVcount;
	uint8_t prevKind;
	unsigned unmatched;
};

// How many vblanks each presented image stayed on screen: 1, 2, 3, 4+.
struct VitaHoldHistogram {
	unsigned held[4];
	unsigned vblanks;	// sum of all holds
	void Reset(void) { held[0] = held[1] = held[2] = held[3] = 0; vblanks = 0; }
	void Add(int n) { if(n >= 1){ held[n > 4 ? 3 : n - 1]++; vblanks += n; } }
	unsigned Total(void) const { return held[0] + held[1] + held[2] + held[3]; }
	// Images per second on a 60 Hz display; 0 without any image.
	double DisplayFPS(void) const { return vblanks ? 60.0 * Total() / vblanks : 0.0; }
};
