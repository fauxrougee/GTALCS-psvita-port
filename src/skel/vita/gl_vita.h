#pragma once

// GL entry point for glfwGetProcAddress: vitaGL's function, or a wrapper that
// works around a vitaGL quirk and/or times the call.
void *VitaGLGetProcAddress(const char *name);

// Before GL loading: count draw/state/upload calls ([VitaPerf] GLCounters).
void VitaGLProfSetCounting(bool enable);
// End of a frame: keep its counters (gameplay frame) or drop them.
void VitaGLProfCommitFrame(void);
void VitaGLProfDiscardFrame(void);
// Writes the kept GL counters, averaged over `frames`, to the log and resets them.
void VitaGLProfReport(int frames);

#ifdef RELCS_BENCHMARK
#include <stdint.h>

// reLCS Benchmark (vita_bench.cpp). The counting wrappers are always used.
// Drops the kept report counters without printing; the frame in progress stays.
void VitaGLProfReset(void);
// Every call since startup, including dropped frames; never reset.
struct VitaGLTotals {
	uint64_t draws, indices;	// indices/vertices asked for by the game
	uint64_t uniforms, texBinds, bufBinds, programs, stateCalls;
	uint64_t vertexBytes, indexBytes, textureBytes;
	uint64_t textureUs;		// glTexImage2D, glTexSubImage2D, glCompressedTexImage2D
	uint64_t shaderCompiles;	// glCompileShader + glLinkProgram
};
void VitaGLGetTotals(VitaGLTotals *out);
// Render experiments, applied only inside the 3D scope (Bench Begin3D/End3D).
// Leaving the scope restores the texture bindings librw expects.
void VitaGLBenchScope3D(bool on);
void VitaGLBenchSetDrawMode(int mode);	// 0 normal, 1 at most 3 indices per draw, 2 no draw
void VitaGLBenchSetFlatTextures(bool on);	// every 2D texture bind -> 1x1 white
#endif
