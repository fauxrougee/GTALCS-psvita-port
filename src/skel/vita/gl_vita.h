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
