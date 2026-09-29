#pragma once
#include <stdint.h>

// All lifecycle calls run on the standalone launcher's main thread.
// Reserve validates the lossless movie and preloads its complete PCM soundtrack.
bool VitaBootMovieReserve();
void VitaBootMovieRelease();
// Poll: 1 new frame,
// 0 waiting, -1 finished, skipped, failed or timed out.
bool VitaBootMovieStart();
int VitaBootMoviePoll(uint32_t *pixels);
void VitaBootMovieClose();
