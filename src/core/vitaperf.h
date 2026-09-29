#pragma once

// Gameplay profiler sections, separate from the original VPROF markers.
// Accounting and host checks are documented in docs/DEVELOPMENT.md.
// Compiled out on platforms other than Vita.
#ifdef PSP2
void VitaProfBegin(const char *name);
void VitaProfEnd(const char *name);
#define VPERF_BEGIN(name) VitaProfBegin(name)
#define VPERF_END(name) VitaProfEnd(name)
// Whole function or block, whatever the return path.
struct VitaPerfSection {
	const char *name;
	VitaPerfSection(const char *name) : name(name) { VitaProfBegin(name); }
	~VitaPerfSection() { VitaProfEnd(name); }
};
#define VPERF_SCOPE(name) VitaPerfSection vperfSection_(name)
#else
#define VPERF_BEGIN(name)
#define VPERF_END(name)
#define VPERF_SCOPE(name)
#endif
