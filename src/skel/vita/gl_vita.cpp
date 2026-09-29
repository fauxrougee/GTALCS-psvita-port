// GL entry points handed to librw (through glad and glfwGetProcAddress):
// vitaGL quirk workarounds, and timing of the GL calls that can stall vitaGL
// (framebuffer copies, uploads, shader builds) for the frame profiler.
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include "gl_vita.h"
#include "vita.h"

// ---- Profiling ------------------------------------------------------------

enum {
	PROF_DrawElements,
	PROF_DrawArrays,
	PROF_BufferData,
	PROF_BufferSubData,
	PROF_TexImage2D,
	PROF_TexSubImage2D,
	PROF_CompressedTexImage2D,
	PROF_CopyTexImage2D,
	PROF_CopyTexSubImage2D,
	PROF_ReadPixels,
	PROF_GenerateMipmap,
	PROF_DeleteTextures,
	PROF_CompileShader,
	PROF_LinkProgram,
	PROF_UseProgram,
	PROF_Uniform,
	PROF_BindFramebuffer,
	PROF_Clear,
	PROF_BindTexture,
	PROF_TexParameter,
	PROF_VertexAttribPointer,
	PROF_FinishFlush,
	PROF_EnableDisable,
	PROF_BlendDepthCull,
	PROF_BindBuffer,
	PROF_COUNT
};

static const char *profNames[PROF_COUNT] = {
	"glDrawElements", "glDrawArrays", "glBufferData", "glBufferSubData",
	"glTexImage2D", "glTexSubImage2D", "glCompressedTexImage2D",
	"glCopyTexImage2D", "glCopyTexSubImage2D", "glReadPixels",
	"glGenerateMipmap", "glDeleteTextures", "glCompileShader", "glLinkProgram",
	"glUseProgram", "glUniform*", "glBindFramebuffer", "glClear",
	"glBindTexture", "glTexParameter*", "glVertexAttribPointer", "glFinish/Flush",
	"glEnable/Disable", "glBlend/Depth/Cull*", "glBindBuffer",
};

// Accumulated per frame, then kept for gameplay frames (VitaGLProfCommitFrame)
// or dropped (VitaGLProfDiscardFrame), like the section profiler.
struct GLProfCounters {
	SceUInt64 time[PROF_COUNT];	// only for timed calls
	unsigned int calls[PROF_COUNT];
	SceUInt64 bytes[PROF_COUNT];
	SceUInt64 indices;		// glDrawElements counts + glDrawArrays vertices
	SceUInt64 vertexBytes, indexBytes, textureBytes;
};
static GLProfCounters prof, committedProf;
static bool countCalls = true;	// [VitaPerf] GLCounters

struct ProfScope {
	int id;
	SceUInt64 start;
	ProfScope(int id) : id(id), start(sceKernelGetProcessTimeWide()) {}
	~ProfScope() {
		prof.time[id] += sceKernelGetProcessTimeWide() - start;
		prof.calls[id]++;
	}
};

void VitaGLProfSetCounting(bool enable) { countCalls = enable; }
void VitaGLProfCommitFrame(void) { committedProf = prof; }
void VitaGLProfDiscardFrame(void) { prof = committedProf; }

// Bytes sent by a texture upload; 0 when only allocating (no pixels).
static SceUInt64
TextureBytes(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
	if(pixels == nullptr || width <= 0 || height <= 0)
		return 0;
	int bpp;
	switch(type){
	case GL_UNSIGNED_SHORT_5_6_5:
	case GL_UNSIGNED_SHORT_4_4_4_4:
	case GL_UNSIGNED_SHORT_5_5_5_1:
		bpp = 2;
		break;
	default:
		bpp = format == GL_RGBA ? 4 : format == GL_RGB ? 3 : format == GL_LUMINANCE_ALPHA ? 2 : 1;
		break;
	}
	return (SceUInt64)width * height * bpp;
}

static void
CountTextureBytes(int id, SceUInt64 bytes)
{
	prof.bytes[id] += bytes;
	prof.textureBytes += bytes;
}

// Buffer uploads by the target currently bound (librw binds before uploading).
static void
CountBufferBytes(GLenum target, GLsizeiptr size, const void *data)
{
	if(data == nullptr || size <= 0)
		return;
	if(target == GL_ELEMENT_ARRAY_BUFFER)
		prof.indexBytes += size;
	else
		prof.vertexBytes += size;
}

// vitaGL's allocator sleeps 1s (up to 4 times) when its pools are exhausted.
// Linked with -Wl,--wrap=sceKernelDelayThread to catch those stalls.
static unsigned int longSleeps;
static SceUInt64 longSleepTime;

extern "C" {
int __real_sceKernelDelayThread(SceUInt delay);

int
__wrap_sceKernelDelayThread(SceUInt delay)
{
	if(delay >= 500000){
		longSleeps++;
		longSleepTime += delay;
		printf("[VITA] sleep of %u ms requested from %p (vitaGL out of memory?)\n",
		       delay / 1000, __builtin_return_address(0));
	}
	return __real_sceKernelDelayThread(delay);
}
}

void
VitaGLProfReport(int frames)
{
	// vglMemFree(VGL_MEM_EXTERNAL) always returns 0: measure the newlib heap
	// (which vitaGL also allocates from) ourselves.
	printf("[VITA]   free: vitaGL RAM %u KB, CDRAM %u KB, PHYCONT %u KB, CDLG %u KB; newlib heap %u KB\n",
	       (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024),
	       (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024), (unsigned)(vglMemFree(VGL_MEM_BUDGET) / 1024),
	       VitaGetFreeMemory() / 1024);
	if(longSleeps)
		printf("[VITA]   %u long sleeps, %.1f ms in total\n", longSleeps, longSleepTime / 1000.0);
	longSleeps = 0;
	longSleepTime = 0;
	const GLProfCounters &p = committedProf;
	if(frames > 0){
		const unsigned *c = p.calls;
		// Without counting wrappers (GLCounters=0) only the rare calls are seen.
		printf("[PERF] gl_per_frame draws=%.1f indices=%.0f state=%.1f textures=%.1f buffers=%.1f programs=%.1f uniforms=%.1f "
		       "fb_copies=%.2f upload_kib vertex=%.1f index=%.1f texture=%.1f counted=%d\n",
		       (double)(c[PROF_DrawElements] + c[PROF_DrawArrays]) / frames, (double)p.indices / frames,
		       (double)(c[PROF_EnableDisable] + c[PROF_BlendDepthCull] + c[PROF_TexParameter] + c[PROF_VertexAttribPointer]) / frames,
		       (double)c[PROF_BindTexture] / frames, (double)c[PROF_BindBuffer] / frames,
		       (double)c[PROF_UseProgram] / frames, (double)c[PROF_Uniform] / frames,
		       (double)(c[PROF_CopyTexImage2D] + c[PROF_CopyTexSubImage2D] + c[PROF_ReadPixels]) / frames,
		       p.vertexBytes / 1024.0 / frames, p.indexBytes / 1024.0 / frames, p.textureBytes / 1024.0 / frames,
		       countCalls);
		for(int i = 0; i < PROF_COUNT; i++){
			if(p.calls[i] == 0)
				continue;
			if(p.time[i])
				printf("[VITA]   %-22s %8.2f ms/frame  %7.1f calls/frame", profNames[i],
				       p.time[i] / 1000.0 / frames, (double)p.calls[i] / frames);
			else
				printf("[VITA]   %-22s %8s ms/frame  %7.1f calls/frame", profNames[i], "-", (double)p.calls[i] / frames);
			if(p.bytes[i])
				printf("  %7.1f KB/frame", p.bytes[i] / 1024.0 / frames);
			printf("\n");
		}
	}
	memset(&prof, 0, sizeof(prof));
	memset(&committedProf, 0, sizeof(committedProf));
}

// Declares real_<name> (vitaGL's entry point), a timed wrapper_<name> and a
// count_<name> that only counts the call (no clock reads). `extra` runs in
// both, before the call: bytes and indices sent.
#define WRAPPED(id, name, params, args, extra) \
	static void (*real_##name) params; \
	static void wrapper_##name params { ProfScope scope(id); extra; real_##name args; } \
	static void count_##name params { prof.calls[id]++; extra; real_##name args; }
#define TIMED(id, name, params, args) WRAPPED(id, name, params, args, (void)0)

WRAPPED(PROF_DrawElements, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices), (mode, count, type, indices),
	prof.indices += count)
WRAPPED(PROF_DrawArrays, glDrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count),
	prof.indices += count)
WRAPPED(PROF_BufferData, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage), (target, size, data, usage),
	(prof.bytes[PROF_BufferData] += data ? size : 0, CountBufferBytes(target, size, data)))
WRAPPED(PROF_BufferSubData, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data), (target, offset, size, data),
	(prof.bytes[PROF_BufferSubData] += data ? size : 0, CountBufferBytes(target, size, data)))
WRAPPED(PROF_TexImage2D, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels),
	(target, level, internalformat, width, height, border, format, type, pixels),
	CountTextureBytes(PROF_TexImage2D, TextureBytes(width, height, format, type, pixels)))
WRAPPED(PROF_TexSubImage2D, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels),
	(target, level, xoffset, yoffset, width, height, format, type, pixels),
	CountTextureBytes(PROF_TexSubImage2D, TextureBytes(width, height, format, type, pixels)))
WRAPPED(PROF_CompressedTexImage2D, glCompressedTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data),
	(target, level, internalformat, width, height, border, imageSize, data),
	CountTextureBytes(PROF_CompressedTexImage2D, data ? imageSize : 0))
TIMED(PROF_CopyTexImage2D, glCopyTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border),
	(target, level, internalformat, x, y, width, height, border))
TIMED(PROF_CopyTexSubImage2D, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height),
	(target, level, xoffset, yoffset, x, y, width, height))
TIMED(PROF_ReadPixels, glReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels),
	(x, y, width, height, format, type, pixels))
TIMED(PROF_GenerateMipmap, glGenerateMipmap, (GLenum target), (target))
TIMED(PROF_DeleteTextures, glDeleteTextures, (GLsizei n, const GLuint *textures), (n, textures))
TIMED(PROF_CompileShader, glCompileShader, (GLuint shader), (shader))
TIMED(PROF_UseProgram, glUseProgram, (GLuint program), (program))
TIMED(PROF_Uniform, glUniform1i, (GLint location, GLint v0), (location, v0))
TIMED(PROF_Uniform, glUniform1f, (GLint location, GLfloat v0), (location, v0))
TIMED(PROF_Uniform, glUniform3fv, (GLint location, GLsizei count, const GLfloat *value), (location, count, value))
TIMED(PROF_Uniform, glUniform4fv, (GLint location, GLsizei count, const GLfloat *value), (location, count, value))
TIMED(PROF_Uniform, glUniform4iv, (GLint location, GLsizei count, const GLint *value), (location, count, value))
TIMED(PROF_Uniform, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value), (location, count, transpose, value))
TIMED(PROF_BindFramebuffer, glBindFramebuffer, (GLenum target, GLuint framebuffer), (target, framebuffer))
TIMED(PROF_Clear, glClear, (GLbitfield mask), (mask))
TIMED(PROF_BindTexture, glBindTexture, (GLenum target, GLuint texture), (target, texture))
TIMED(PROF_TexParameter, glTexParameteri, (GLenum target, GLenum pname, GLint param), (target, pname, param))
TIMED(PROF_TexParameter, glTexParameterf, (GLenum target, GLenum pname, GLfloat param), (target, pname, param))
TIMED(PROF_VertexAttribPointer, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer),
	(index, size, type, normalized, stride, pointer))
TIMED(PROF_FinishFlush, glFinish, (void), ())
TIMED(PROF_FinishFlush, glFlush, (void), ())
TIMED(PROF_EnableDisable, glEnable, (GLenum cap), (cap))
TIMED(PROF_EnableDisable, glDisable, (GLenum cap), (cap))
TIMED(PROF_BlendDepthCull, glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor))
TIMED(PROF_BlendDepthCull, glDepthFunc, (GLenum func), (func))
TIMED(PROF_BlendDepthCull, glDepthMask, (GLboolean flag), (flag))
TIMED(PROF_BlendDepthCull, glCullFace, (GLenum mode), (mode))
TIMED(PROF_BindBuffer, glBindBuffer, (GLenum target, GLuint buffer), (target, buffer))

// ---- vitaGL workarounds -----------------------------------------------------

// vitaGL's glBindAttribLocation needs the vertex shader to be attached already,
// while librw binds right after glCreateProgram (valid GL: bindings only take
// effect at link time). Queue the bindings and apply them just before linking.
struct PendingAttribBinding {
	GLuint program;
	GLuint index;
	char name[32];
};
static PendingAttribBinding pendingBindings[32];
static int numPendingBindings;

static void
DeferredBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
	if(numPendingBindings == 32 || strlen(name) >= sizeof(pendingBindings[0].name)){
		glBindAttribLocation(program, index, name);
		return;
	}
	PendingAttribBinding *b = &pendingBindings[numPendingBindings++];
	b->program = program;
	b->index = index;
	strcpy(b->name, name);
}

static void
LinkProgramWithBindings(GLuint program)
{
	ProfScope scope(PROF_LinkProgram);
	int kept = 0;
	for(int i = 0; i < numPendingBindings; i++){
		PendingAttribBinding *b = &pendingBindings[i];
		if(b->program == program)
			glBindAttribLocation(b->program, b->index, b->name);
		else
			pendingBindings[kept++] = *b;
	}
	numPendingBindings = kept;
	glLinkProgram(program);
}

// ---- Lookup -------------------------------------------------------------------

#define ENTRY(name, frequent) { #name, (void**)&real_##name, (void*)wrapper_##name, (void*)count_##name, frequent }

static struct {
	const char *name;
	void **real;
	void *timed;
	void *counted;
	bool frequent;	// timed only with VITA_DETAILED_GL_PROFILE
} entries[] = {
	// Draw/state calls occur thousands of times per frame: two clock reads each
	// would add measurable CPU time. Normal builds count them ([VitaPerf]
	// GLCounters=1, one increment each) or return vitaGL's entry points directly.
	ENTRY(glDrawElements, true), ENTRY(glDrawArrays, true), ENTRY(glBufferData, true),
	ENTRY(glBufferSubData, true), ENTRY(glUseProgram, true), ENTRY(glUniform1i, true),
	ENTRY(glUniform1f, true), ENTRY(glUniform3fv, true), ENTRY(glUniform4fv, true),
	ENTRY(glUniform4iv, true), ENTRY(glUniformMatrix4fv, true), ENTRY(glBindFramebuffer, true),
	ENTRY(glClear, true), ENTRY(glBindTexture, true), ENTRY(glTexParameteri, true),
	ENTRY(glTexParameterf, true), ENTRY(glVertexAttribPointer, true), ENTRY(glEnable, true),
	ENTRY(glDisable, true), ENTRY(glBlendFunc, true), ENTRY(glDepthFunc, true),
	ENTRY(glDepthMask, true), ENTRY(glCullFace, true), ENTRY(glBindBuffer, true),
	// Rare calls that can stall vitaGL: always timed.
	ENTRY(glTexImage2D, false), ENTRY(glTexSubImage2D, false), ENTRY(glCompressedTexImage2D, false),
	ENTRY(glCopyTexImage2D, false), ENTRY(glCopyTexSubImage2D, false), ENTRY(glReadPixels, false),
	ENTRY(glGenerateMipmap, false), ENTRY(glDeleteTextures, false), ENTRY(glCompileShader, false),
	ENTRY(glFinish, false), ENTRY(glFlush, false),
};

void *
VitaGLGetProcAddress(const char *name)
{
	if(strcmp(name, "glBindAttribLocation") == 0)
		return (void*)DeferredBindAttribLocation;
	if(strcmp(name, "glLinkProgram") == 0)
		return (void*)LinkProgramWithBindings;

	void *proc = vglGetProcAddress(name);
	if(proc == nullptr)
		return nullptr;
	for(auto &e : entries){
		if(strcmp(name, e.name) == 0){
			*e.real = proc;
#ifndef VITA_DETAILED_GL_PROFILE
			if(e.frequent)
				return countCalls ? e.counted : proc;
#endif
			return e.timed;
		}
	}
	return proc;
}
