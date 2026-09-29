#pragma once

// Named profiler sections for the PS Vita build (see src/skel/vita/vita.cpp),
// compiled out everywhere else.
#ifdef PSP2
void VitaProfBegin(const char *name);
void VitaProfEnd(const char *name);
#define VPROF_BEGIN(name) VitaProfBegin(name)
#define VPROF_END(name) VitaProfEnd(name)
#else
#define VPROF_BEGIN(name)
#define VPROF_END(name)
#endif
