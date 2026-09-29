#if defined(PSP2) || defined(VITA_PERF_HOST_TEST)
#include <algorithm>
#include <string.h>
#include "vita_perf_stats.h"

void
VitaSampleSeries::Reset(void)
{
	count = 0;
	total = 0;
	minUs = UINT32_MAX;
	maxUs = 0;
	over33 = over50 = 0;
	windowUs = 0;
	windowFrames = fpsWindows = 0;
	fpsMin = fpsMax = -1.0f;
}

void
VitaSampleSeries::Add(uint32_t us)
{
	if(count < CAPACITY)
		samples[count] = us;
	count++;
	total += us;
	minUs = std::min(minUs, us);
	maxUs = std::max(maxUs, us);
	// Exact thresholds: 33.3 ms is a 30 fps frame, 50 ms a 20 fps one.
	if(us > 33333) over33++;
	if(us > 50000) over50++;

	windowUs += us;
	windowFrames++;
	if(windowUs >= 1000000){
		float fps = (float)(windowFrames * 1000000.0 / windowUs);
		fpsMin = fpsWindows == 0 ? fps : std::min(fpsMin, fps);
		fpsMax = fpsWindows == 0 ? fps : std::max(fpsMax, fps);
		fpsWindows++;
		windowUs = 0;
		windowFrames = 0;
	}
}

// Nearest rank: the smallest sample with at least q of the samples at or below it.
static double
Percentile(const uint32_t *sorted, unsigned n, double q)
{
	unsigned rank = (unsigned)(q * n + 0.999999);
	if(rank < 1) rank = 1;
	if(rank > n) rank = n;
	return sorted[rank - 1] / 1000.0;
}

void
VitaSampleSeries::Summarise(VitaSampleSummary *out, uint32_t *scratch) const
{
	memset(out, 0, sizeof(*out));
	out->fpsWindows = fpsWindows;
	out->fpsMin = fpsMin;
	out->fpsMax = fpsMax;
	if(count == 0)
		return;
	unsigned stored = std::min(count, (unsigned)CAPACITY);
	memcpy(scratch, samples, stored * sizeof(uint32_t));
	std::sort(scratch, scratch + stored);
	out->count = count;
	out->meanMs = total / 1000.0 / count;
	out->medianMs = Percentile(scratch, stored, 0.50);
	out->p95Ms = Percentile(scratch, stored, 0.95);
	out->p99Ms = Percentile(scratch, stored, 0.99);
	out->minMs = minUs / 1000.0;
	out->maxMs = maxUs / 1000.0;
	out->over33 = over33;
	out->over50 = over50;
}

uint32_t
VitaFlipMatcher::Submit(uint64_t time, uint8_t kind)
{
	submits[count % RING].time = time;
	submits[count % RING].kind = kind;
	return count++;
}

bool
VitaFlipMatcher::Flip(uint32_t flipIndex, int vcount, uint64_t time, VitaHoldHistogram *holds, VitaSampleSeries *latencies)
{
	uint32_t index = flipIndex - offset;
	if(count == 0 || index >= count){
		// Unknown callback: never count the latest image twice. Pair the next
		// callback with the next submission, and break the hold series.
		unmatched++;
		offset = flipIndex + 1 - count;
		havePrev = false;
		return false;
	}
	uint8_t kind = count - index <= RING ? submits[index % RING].kind : (uint8_t)OTHER;
	if(havePrev && prevKind == GAME)
		holds->Add(vcount - prevVcount);
	havePrev = true;
	prevVcount = vcount;
	prevKind = kind;
	if(kind != GAME)
		return false;
	uint64_t submitted = submits[index % RING].time;
	latencies->Add(time > submitted ? (uint32_t)(time - submitted) : 0);
	return true;
}

uint64_t
VitaFramePacer::Remaining(uint64_t now)
{
	if(next == 0 || now >= next + period)
		next = now;
	if(now < next)
		return next - now;
	next += period;
	return 0;
}
#endif
