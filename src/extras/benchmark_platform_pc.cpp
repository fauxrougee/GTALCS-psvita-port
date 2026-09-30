// reLCS Benchmark platform layer for Windows (benchmark_platform.h), used to
// validate the scenes on PC: timing, files and keyboard. GPU timing, render
// experiments, profiler sections and counters are Vita-only; Caps() is 0 and
// the director skips or marks the tests that need them.
#if defined(RELCS_BENCHMARK) && !defined(PSP2)
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "benchmark_platform.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")	// RawButtons
#endif
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace BenchPlatform {

void
Init(void)
{
	NowUs();	// fixes the clock origin
	printf("[BENCH] platform: PC, no GPU timing, render experiments or profiler sections\n");
}

uint32_t Caps(void) { return 0; }

// Microseconds since the process started, on the performance counter.
uint64_t
NowUs(void)
{
#ifdef _WIN32
	static LARGE_INTEGER freq, start;
	static uint64_t startOffsetUs;
	LARGE_INTEGER now;
	if(freq.QuadPart == 0){
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&start);
		FILETIME creation, exited, kernel, user, current;
		if(GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user)){
			GetSystemTimeAsFileTime(&current);
			uint64_t c = ((uint64_t)creation.dwHighDateTime << 32) | creation.dwLowDateTime;
			uint64_t n = ((uint64_t)current.dwHighDateTime << 32) | current.dwLowDateTime;
			if(n > c)
				startOffsetUs = (n - c) / 10;	// 100 ns units
		}
	}
	QueryPerformanceCounter(&now);
	uint64_t ticks = (uint64_t)(now.QuadPart - start.QuadPart);
	uint64_t f = (uint64_t)freq.QuadPart;
	return startOffsetUs + ticks / f * 1000000 + ticks % f * 1000000 / f;
#else
	static struct timespec start;
	struct timespec now;
	if(start.tv_sec == 0 && start.tv_nsec == 0)
		clock_gettime(CLOCK_MONOTONIC, &start);
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)(now.tv_sec - start.tv_sec) * 1000000 + (now.tv_nsec - start.tv_nsec) / 1000;
#endif
}

void Tick(void) {}

const char *ResultsRoot(void) { return "benchmark/"; }

void
Timestamp(char *buf, int size)
{
	time_t now = time(NULL);
	struct tm t;
#ifdef _WIN32
	localtime_s(&t, &now);
#else
	localtime_r(&now, &t);
#endif
	snprintf(buf, size, "%04d-%02d-%02d_%02d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
	         t.tm_hour, t.tm_min, t.tm_sec);
}

bool
MakeDir(const char *path)
{
	char dir[260];
	int len = snprintf(dir, sizeof(dir), "%s", path);
	if(len <= 0 || len >= (int)sizeof(dir))
		return false;
	while(len > 1 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
		dir[--len] = '\0';
#ifdef _WIN32
	if(_mkdir(dir) == 0 || errno == EEXIST)
		return true;
#else
	if(mkdir(dir, 0777) == 0 || errno == EEXIST)
		return true;
#endif
	printf("[BENCH] mkdir %s failed (errno %d)\n", dir, errno);
	return false;
}

bool
WriteFile(const char *path, const void *data, uint32_t size)
{
	FILE *f = fopen(path, "wb");
	if(f == NULL){
		printf("[BENCH] cannot create %s\n", path);
		return false;
	}
	bool ok = fwrite(data, 1, size, f) == size;
	ok = fclose(f) == 0 && ok;
	if(!ok)
		printf("[BENCH] write %s failed (%u bytes)\n", path, size);
	return ok;
}

int
ReadSettingsFile(char *buf, int size)
{
	if(size <= 0)
		return -1;
	buf[0] = '\0';
	FILE *f = fopen("benchmark.ini", "rb");
	if(f == NULL)
		return -1;
	int n = (int)fread(buf, 1, size - 1, f);
	fclose(f);
	buf[n] = '\0';
	return n;
}

// Compile date as "pc-YYYYMMDD" from __DATE__ ("Sep 30 2026").
static void
BuildId(char *buf, int size)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	const char *date = __DATE__;
	int month = 0;
	for(int i = 0; i < 12; i++)
		if(strncmp(date, months + i * 3, 3) == 0)
			month = i + 1;
	int day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + (date[5] - '0');
	snprintf(buf, size, "pc-%.4s%02d%02d", date + 7, month, day);
}

void
DeviceInfo(BenchDeviceInfo *out)
{
	memset(out, 0, sizeof(*out));
	snprintf(out->platform, sizeof(out->platform), "Windows");
	snprintf(out->model, sizeof(out->model), "unknown CPU");
#if defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))
	int regs[4];
	__cpuid(regs, 0x80000000);
	if((unsigned)regs[0] >= 0x80000004u){
		char brand[49];
		for(int i = 0; i < 3; i++){
			__cpuid(regs, 0x80000002 + i);
			memcpy(brand + i * 16, regs, 16);
		}
		brand[48] = '\0';
		const char *b = brand;
		while(*b == ' ')
			b++;
		snprintf(out->model, sizeof(out->model), "%s", b);
	}
#endif
	snprintf(out->firmware, sizeof(out->firmware), "-");
	BuildId(out->build, sizeof(out->build));
#ifdef RW_GL3
	snprintf(out->gpu, sizeof(out->gpu), "OpenGL 3 (librw)");
#else
	snprintf(out->gpu, sizeof(out->gpu), "Direct3D 9");
#endif
	snprintf(out->notes, sizeof(out->notes), "PC validation run: timings only, no GPU or profiler data");
}

uint32_t
RawButtons(void)
{
#ifdef _WIN32
	// Only while the game window has the focus
	HWND window = GetForegroundWindow();
	DWORD pid = 0;
	if(window == NULL || GetWindowThreadProcessId(window, &pid) == 0 || pid != GetCurrentProcessId())
		return 0;
	static const struct { int key; uint32_t button; } keys[] = {
		{ VK_RETURN, BENCH_BTN_CROSS }, { VK_BACK, BENCH_BTN_CIRCLE }, { VK_ESCAPE, BENCH_BTN_START },
		{ VK_TAB, BENCH_BTN_SELECT }, { 'T', BENCH_BTN_TRIANGLE }, { VK_UP, BENCH_BTN_UP }, { VK_DOWN, BENCH_BTN_DOWN },
	};
	uint32_t out = 0;
	for(int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
		if(GetAsyncKeyState(keys[i].key) & 0x8000)
			out |= keys[i].button;
	return out;
#else
	return 0;
#endif
}

void ReadCounters(BenchCounters *out) { memset(out, 0, sizeof(*out)); }
void ReadMemory(BenchMemory *out) { memset(out, 0, sizeof(*out)); }
void ReadThreadTimes(BenchThreadTimes *out) { memset(out, 0, sizeof(*out)); }
void ReadCpuLoad(BenchCpuLoad *out) { memset(out, 0, sizeof(*out)); }

void FrameSubmitted(uint32_t) {}
bool PollGpuDone(uint32_t *, uint64_t *) { return false; }
void SetGpuSync(bool) {}
uint32_t LastGpuSyncWaitUs(void) { return 0; }

bool SetViewportScale(float) { return false; }
bool SetDrawMode(int) { return false; }
bool SetFlatTextures(bool) { return false; }
void Begin3D(void) {}
void End3D(void) {}

int SectionCount(void) { return 0; }
const char *SectionName(int) { return ""; }
int SectionParent(int) { return -1; }
int SectionDepth(int) { return 0; }
uint32_t SectionFrameUs(int) { return 0; }
uint32_t SectionFrameCalls(int) { return 0; }

void SetPeriodicReports(bool) {}
void LogWindowBegin(void) {}

void
LogWindowEnd(const char *scene)
{
	printf("[BENCH] test done: %s\n", scene ? scene : "-");
}

void FlushLog(void) { fflush(stdout); }

} // namespace BenchPlatform

#endif
