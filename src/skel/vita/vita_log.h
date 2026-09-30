#pragma once

#ifdef PSP2
#include <stdint.h>

// One writer owns the file. Producers only copy into a bounded memory queue;
// a full queue drops diagnostic bytes instead of blocking the game on storage.
bool VitaLogInit(const char *path);
void VitaLogShutdown(void); // drain on normal exit only; never during a frame

struct VitaLogStats {
	uint32_t queued, peak, dropped, truncated, errors, writeMaxUs;
	bool running;
};
VitaLogStats VitaLogGetStats(void);
#ifdef RELCS_BENCHMARK
// Wakes the writer and waits until everything queued is on disk (benchmark,
// between tests only). False on timeout.
bool VitaLogWaitIdle(unsigned timeoutUs);
#endif
#endif
