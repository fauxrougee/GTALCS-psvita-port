#ifdef PSP2
#include <atomic>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include "vita_log.h"
#include "vita_perf.h"

namespace {
constexpr unsigned CAPACITY = 128 * 1024, BATCH = 16 * 1024;
char queue[CAPACITY], batch[BATCH];
unsigned head, used, peak;
SceUID file = -1, mutex = -1, wake = -1, writer = -1;
std::atomic<bool> running(false), stopping(false);
std::atomic<uint32_t> dropped(0), truncated(0), errors(0), writeMaxUs(0);
#ifdef RELCS_BENCHMARK
unsigned batchesTaken;				// under the mutex
std::atomic<unsigned> batchesWritten(0);	// by the writer, after each batch
#endif

// Never hold this mutex while formatting, writing, waiting for the disk or
// joining the worker. All queue indices are protected by the same mutex.
void Lock() { sceKernelLockMutex(mutex, 1, nullptr); }
void Unlock() { sceKernelUnlockMutex(mutex, 1); }

bool Enqueue(const char *data, size_t size, bool newline = false)
{
	if(!running.load(std::memory_order_acquire)) return false;
	Lock();
	if(stopping.load(std::memory_order_relaxed) || size > CAPACITY - used ||
	   (newline && size == CAPACITY - used)){
		dropped.fetch_add(uint32_t(size) + unsigned(newline), std::memory_order_relaxed);
		Unlock();
		return false;
	}
	unsigned tail = (head + used) % CAPACITY;
	unsigned first = unsigned(size) < CAPACITY - tail ? unsigned(size) : CAPACITY - tail;
	memcpy(queue + tail, data, first);
	memcpy(queue, data + first, size - first);
	used += unsigned(size);
	if(newline) { queue[(head + used) % CAPACITY] = '\n'; ++used; }
	if(used > peak) peak = used;
	const bool fullBatch = used >= BATCH;
	Unlock();
	if(fullBatch) sceKernelSignalSema(wake, 1); // a pending signal is sufficient
	return true;
}

unsigned TakeBatch()
{
	Lock();
	unsigned count = used < BATCH ? used : BATCH;
	unsigned first = count < CAPACITY - head ? count : CAPACITY - head;
	memcpy(batch, queue + head, first);
	memcpy(batch + first, queue, count - first);
	head = (head + count) % CAPACITY;
	used -= count;
#ifdef RELCS_BENCHMARK
	if(count) batchesTaken++;
#endif
	Unlock();
	return count;
}

int WriteThread(SceSize, void *)
{
	VitaPerfRegisterThread("LogWriter");
	for(;;){
		SceUInt timeout = 250000;
		sceKernelWaitSema(wake, 1, &timeout);
		unsigned count;
		while((count = TakeBatch()) != 0){
			const uint64_t start = sceKernelGetProcessTimeWide();
			unsigned done = 0;
			while(done < count){
				int n = sceIoWrite(file, batch + done, count - done);
				if(n <= 0 || unsigned(n) > count - done){
					errors.fetch_add(1, std::memory_order_relaxed);
					dropped.fetch_add(count - done, std::memory_order_relaxed);
					break; // no retry loop on a missing/full storage device
				}
				done += unsigned(n);
			}
			uint32_t elapsed = uint32_t(sceKernelGetProcessTimeWide() - start);
			if(elapsed > writeMaxUs.load(std::memory_order_relaxed))
				writeMaxUs.store(elapsed, std::memory_order_relaxed);
#ifdef RELCS_BENCHMARK
			batchesWritten.fetch_add(1, std::memory_order_release);
#endif
		}
		if(stopping.load(std::memory_order_acquire)) break;
	}
	return 0;
}

bool Console(FILE *stream) { return stream == stdout || stream == stderr; }

int Format(const char *format, va_list ap)
{
	char text[2048]; // bounded, including for shader/compiler diagnostics
	int n = vsnprintf(text, sizeof(text), format, ap);
	if(n < 0) return n;
	size_t size = size_t(n);
	if(size >= sizeof(text)){
		static const char marker[] = "...[LOG truncated]\n";
		size = sizeof(text) - 1;
		memcpy(text + size - (sizeof(marker) - 1), marker, sizeof(marker) - 1);
		truncated.fetch_add(1, std::memory_order_relaxed);
	}
	return Enqueue(text, size) ? int(size) : -1;
}
}

bool VitaLogInit(const char *path)
{
	file = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if(file < 0) return false;
	mutex = sceKernelCreateMutex("reLCS log queue", 0, 0, nullptr);
	if(mutex >= 0) wake = sceKernelCreateSema("reLCS log wake", 0, 0, 1, nullptr);
	if(wake >= 0) writer = sceKernelCreateThread("reLCS log writer", WriteThread,
		0x10000100, 32 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_ALL, nullptr);
	if(writer < 0 || sceKernelStartThread(writer, 0, nullptr) < 0){
		if(writer >= 0) sceKernelDeleteThread(writer);
		if(wake >= 0) sceKernelDeleteSema(wake);
		if(mutex >= 0) sceKernelDeleteMutex(mutex);
		sceIoClose(file);
		writer = wake = mutex = file = -1;
		return false;
	}
	running.store(true, std::memory_order_release);
	atexit(VitaLogShutdown);
	return true;
}

void VitaFlushLog(void)
{
	// Request a drain; never fflush or join the disk writer from the frame loop.
	if(running.load(std::memory_order_acquire)) sceKernelSignalSema(wake, 1);
}

void VitaLogShutdown(void)
{
	if(!running.load(std::memory_order_acquire)) return;
	Lock();
	stopping.store(true, std::memory_order_release);
	Unlock();
	sceKernelSignalSema(wake, 1);
	SceUInt timeout = 2000000;
	if(sceKernelWaitThreadEnd(writer, nullptr, &timeout) < 0) return;
	running.store(false, std::memory_order_release);
	sceIoClose(file);
	// The process is exiting. Keep the small kernel objects alive for any late
	// producers/destructors, rather than deleting a mutex they may still enter.
}

#ifdef RELCS_BENCHMARK
bool VitaLogWaitIdle(unsigned timeoutUs)
{
	if(!running.load(std::memory_order_acquire)) return true;
	const uint64_t start = sceKernelGetProcessTimeWide();
	for(;;){
		sceKernelSignalSema(wake, 1);
		Lock();
		const bool idle = used == 0 && batchesTaken == batchesWritten.load(std::memory_order_acquire);
		Unlock();
		if(idle) return true;
		if(sceKernelGetProcessTimeWide() - start >= timeoutUs) return false;
		sceKernelDelayThread(2000);
	}
}
#endif

VitaLogStats VitaLogGetStats(void)
{
	VitaLogStats s = {};
	s.running = running.load(std::memory_order_acquire);
	if(s.running){ Lock(); s.queued = used; s.peak = peak; Unlock(); }
	s.dropped = dropped.load(std::memory_order_relaxed);
	s.truncated = truncated.load(std::memory_order_relaxed);
	s.errors = errors.load(std::memory_order_relaxed);
	s.writeMaxUs = writeMaxUs.load(std::memory_order_relaxed);
	return s;
}

// Keep ordinary file I/O (saves, shaders, assets) on the original libc path.
// Include stderr variants because compilers lower constant fprintf to fwrite
// or fputs. No shared buffered FILE is used by the diagnostic producers.
extern "C" {
int __real_vprintf(const char *, va_list);
int __real_puts(const char *);
int __real_putchar(int);
int __real_vfprintf(FILE *, const char *, va_list);
int __real_fputs(const char *, FILE *);
int __real_fputc(int, FILE *);
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
int __real_fflush(FILE *);

int __wrap_vprintf(const char *f, va_list ap) { return running.load() ? Format(f, ap) : __real_vprintf(f, ap); }
int __wrap_printf(const char *f, ...) { va_list ap; va_start(ap, f); int n = __wrap_vprintf(f, ap); va_end(ap); return n; }
int __wrap_puts(const char *s) { return running.load() ? (Enqueue(s, strlen(s), true) ? 0 : EOF) : __real_puts(s); }
int __wrap_putchar(int c) { char v = char(c); return running.load() ? (Enqueue(&v, 1) ? (unsigned char)c : EOF) : __real_putchar(c); }
int __wrap_vfprintf(FILE *s, const char *f, va_list ap) { return running.load() && Console(s) ? Format(f, ap) : __real_vfprintf(s, f, ap); }
int __wrap_fprintf(FILE *s, const char *f, ...) { va_list ap; va_start(ap, f); int n = __wrap_vfprintf(s, f, ap); va_end(ap); return n; }
int __wrap_fputs(const char *s, FILE *f) { return running.load() && Console(f) ? (Enqueue(s, strlen(s)) ? 0 : EOF) : __real_fputs(s, f); }
int __wrap_fputc(int c, FILE *f) { char v = char(c); return running.load() && Console(f) ? (Enqueue(&v, 1) ? (unsigned char)c : EOF) : __real_fputc(c, f); }
size_t __wrap_fwrite(const void *p, size_t s, size_t n, FILE *f)
{
	if(!running.load() || !Console(f)) return __real_fwrite(p, s, n, f);
	if(!s || !n || n > SIZE_MAX / s) return 0;
	return Enqueue(static_cast<const char *>(p), s * n) ? n : 0;
}
int __wrap_fflush(FILE *f)
{
	if(running.load() && (f == nullptr || Console(f))) VitaFlushLog();
	return f != nullptr && running.load() && Console(f) ? 0 : __real_fflush(f);
}
}
#endif
