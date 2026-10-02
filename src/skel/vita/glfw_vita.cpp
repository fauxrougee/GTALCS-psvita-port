// GLFW 3.3 subset for PS Vita (see GLFW/glfw3.h), so librw's GLFW device and
// the glfw skeleton run unchanged. Rendering goes through vitaGL (GLES 2.0),
// input through sceCtrl (buttons, sticks) and sceTouch (L2/R2/L3/R3 zones).
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include "GLFW/glfw3.h"
#include "gl_vita.h"
#include "vita.h"
#include "vita_loading.h"
#include "vita_perf.h"
#include "PadCheats.h"

#define VITA_SCREEN_WIDTH  960
#define VITA_SCREEN_HEIGHT 544

// Vita RAM that vitaGL leaves to the rest of the app, on top of the newlib heap.
#define VITA_GL_RAM_THRESHOLD (16 * 1024 * 1024)

struct GLFWmonitor { int dummy; };
struct GLFWwindow { int shouldClose; };

static GLFWmonitor vitaMonitor;
static GLFWmonitor *vitaMonitors[1] = { &vitaMonitor };
static GLFWwindow vitaWindow;
static const GLFWvidmode vitaVideoMode = { VITA_SCREEN_WIDTH, VITA_SCREEN_HEIGHT, 8, 8, 8, 60 };

static int hintClientApi = GLFW_OPENGL_API;
static int hintMajor = 1;
static int hintMinor = 0;
static bool glInitialised;

static GLFWgamepadstate padState;
static unsigned int cheatButtons;
static unsigned char joyButtons[GLFW_GAMEPAD_BUTTON_LAST + 1];
static float joyAxes[GLFW_GAMEPAD_AXIS_LAST + 1];

// Touch panel sizes in sceTouch units.
#define FRONT_TOUCH_WIDTH  1920
#define REAR_TOUCH_WIDTH   1920

static float
StickAxis(unsigned char v)
{
	float f = ((float)v - 128.0f) / 127.0f;
	if(f < -1.0f) f = -1.0f;
	if(f > 1.0f) f = 1.0f;
	return f;
}

// Sets *left / *right when a finger touches the left / right half of a panel.
static void
ReadTouchHalves(SceTouchPortType port, int width, bool *left, bool *right)
{
	SceTouchData touch;
	*left = *right = false;
	if(sceTouchPeek(port, &touch, 1) < 1)
		return;
	for(int i = 0; i < touch.reportNum; i++){
		if(touch.report[i].x < width / 2)
			*left = true;
		else
			*right = true;
	}
}

static void
UpdatePad(void)
{
	SceCtrlData ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	// Ext2 also reports L2/R2/L3/R3 of a DualShock on PS TV.
	sceCtrlPeekBufferPositiveExt2(0, &ctrl, 1);

	bool rearL, rearR, frontL, frontR;
	ReadTouchHalves(SCE_TOUCH_PORT_BACK, REAR_TOUCH_WIDTH, &rearL, &rearR);
	ReadTouchHalves(SCE_TOUCH_PORT_FRONT, FRONT_TOUCH_WIDTH, &frontL, &frontR);

	unsigned int b = ctrl.buttons;
	// Cheats use the PSP layout and must not inherit action remapping or touch.
	cheatButtons = 0;
	const struct { unsigned int hardware, cheat; } masks[] = {
	    {SCE_CTRL_TRIANGLE, PadCheats::Triangle}, {SCE_CTRL_CIRCLE, PadCheats::Circle},
	    {SCE_CTRL_CROSS, PadCheats::Cross}, {SCE_CTRL_SQUARE, PadCheats::Square},
	    {SCE_CTRL_UP, PadCheats::Up}, {SCE_CTRL_DOWN, PadCheats::Down},
	    {SCE_CTRL_LEFT, PadCheats::Left}, {SCE_CTRL_RIGHT, PadCheats::Right},
	    {SCE_CTRL_LTRIGGER | SCE_CTRL_L1, PadCheats::L},
	    {SCE_CTRL_RTRIGGER | SCE_CTRL_R1, PadCheats::R}};
	for(const auto &mask: masks)
		if(b & mask.hardware) cheatButtons |= mask.cheat;
	unsigned char *btn = padState.buttons;
	btn[GLFW_GAMEPAD_BUTTON_A] = !!(b & SCE_CTRL_CROSS);
	btn[GLFW_GAMEPAD_BUTTON_B] = !!(b & SCE_CTRL_CIRCLE);
	btn[GLFW_GAMEPAD_BUTTON_X] = !!(b & SCE_CTRL_SQUARE);
	btn[GLFW_GAMEPAD_BUTTON_Y] = !!(b & SCE_CTRL_TRIANGLE);
	btn[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = !!(b & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1));
	btn[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = !!(b & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1));
	btn[GLFW_GAMEPAD_BUTTON_BACK] = !!(b & SCE_CTRL_SELECT);
	btn[GLFW_GAMEPAD_BUTTON_START] = !!(b & SCE_CTRL_START);
	btn[GLFW_GAMEPAD_BUTTON_GUIDE] = 0;
	btn[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] = (b & SCE_CTRL_L3) || frontL;
	btn[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] = (b & SCE_CTRL_R3) || frontR;
	btn[GLFW_GAMEPAD_BUTTON_DPAD_UP] = !!(b & SCE_CTRL_UP);
	btn[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = !!(b & SCE_CTRL_RIGHT);
	btn[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = !!(b & SCE_CTRL_DOWN);
	btn[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = !!(b & SCE_CTRL_LEFT);

	float *ax = padState.axes;
	ax[GLFW_GAMEPAD_AXIS_LEFT_X] = StickAxis(ctrl.lx);
	ax[GLFW_GAMEPAD_AXIS_LEFT_Y] = StickAxis(ctrl.ly);
	ax[GLFW_GAMEPAD_AXIS_RIGHT_X] = StickAxis(ctrl.rx);
	ax[GLFW_GAMEPAD_AXIS_RIGHT_Y] = StickAxis(ctrl.ry);
	// GLFW triggers go from -1 (released) to 1 (pressed).
	ax[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = ((b & SCE_CTRL_L2) || rearL) ? 1.0f : -1.0f;
	ax[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = ((b & SCE_CTRL_R2) || rearR) ? 1.0f : -1.0f;

	memcpy(joyButtons, padState.buttons, sizeof(joyButtons));
	memcpy(joyAxes, padState.axes, sizeof(joyAxes));
}

unsigned int VitaGetCheatButtons(void) { return cheatButtons; }

extern "C" {

int
glfwInit(void)
{
	sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	for(int i = 0; i <= GLFW_GAMEPAD_AXIS_LAST; i++)
		padState.axes[i] = 0.0f;
	padState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;
	padState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;
	return GLFW_TRUE;
}

void glfwTerminate(void) {}

GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun) { return nullptr; }

double
glfwGetTime(void)
{
	return (double)sceKernelGetProcessTimeWide() / 1000000.0;
}

GLFWmonitor **
glfwGetMonitors(int *count)
{
	*count = 1;
	return vitaMonitors;
}

GLFWmonitor *glfwGetPrimaryMonitor(void) { return &vitaMonitor; }
const char *glfwGetMonitorName(GLFWmonitor *) { return "PS Vita"; }

const GLFWvidmode *
glfwGetVideoModes(GLFWmonitor *, int *count)
{
	*count = 1;
	return &vitaVideoMode;
}

const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *) { return &vitaVideoMode; }

void
glfwWindowHint(int hint, int value)
{
	switch(hint){
	case GLFW_CLIENT_API: hintClientApi = value; break;
	case GLFW_CONTEXT_VERSION_MAJOR: hintMajor = value; break;
	case GLFW_CONTEXT_VERSION_MINOR: hintMinor = value; break;
	}
}

GLFWwindow *
glfwCreateWindow(int, int, const char *, GLFWmonitor *, GLFWwindow *)
{
	// vitaGL is an OpenGL ES 2.0 implementation: refuse the other profiles
	// so librw falls through its list down to ES 2.0.
	if(hintClientApi != GLFW_OPENGL_ES_API || hintMajor != 2 || hintMinor != 0)
		return nullptr;
	if(!glInitialised){
		vglInitExtended(0, VITA_SCREEN_WIDTH, VITA_SCREEN_HEIGHT, VITA_GL_RAM_THRESHOLD, SCE_GXM_MULTISAMPLE_NONE);
		VitaPerfGLReady();
		glInitialised = true;
	}
	vitaWindow.shouldClose = 0;
	return &vitaWindow;
}

void glfwDestroyWindow(GLFWwindow *) {}
int glfwWindowShouldClose(GLFWwindow *window) { return window ? window->shouldClose : 0; }
void glfwSetWindowShouldClose(GLFWwindow *window, int value) { if(window) window->shouldClose = value; }

void
glfwGetWindowSize(GLFWwindow *, int *width, int *height)
{
	if(width) *width = VITA_SCREEN_WIDTH;
	if(height) *height = VITA_SCREEN_HEIGHT;
}

void glfwSetWindowSize(GLFWwindow *, int, int) {}
void glfwSetWindowPos(GLFWwindow *, int, int) {}

void
glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height)
{
	glfwGetWindowSize(window, width, height);
}

GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow *, GLFWwindowfocusfun) { return nullptr; }
GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow *, GLFWwindowiconifyfun) { return nullptr; }
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow *, GLFWframebuffersizefun) { return nullptr; }

void glfwMakeContextCurrent(GLFWwindow *) {}
void
glfwSwapBuffers(GLFWwindow *)
{
	// Frame statistics and the log report: vita_perf.cpp.
	VitaPerfSwapBegin();
	vglSwapBuffers(GL_FALSE);
	VitaLoadingRelease();
	VitaPerfSwapEnd();
	VitaRecordPresentedFrame();
}

void
glfwSwapInterval(int interval)
{
	// vitaGL's display queue waits this many vblanks after each flip;
	// eglSwapInterval takes any count, from the next flip on.
	static int current = -1;
	int n = VitaPerfSwapInterval(interval);
	if(n != current){
		eglSwapInterval(EGL_NO_DISPLAY, n);
		current = n;
	}
}

GLFWglproc glfwGetProcAddress(const char *procname) { return (GLFWglproc)VitaGLGetProcAddress(procname); }

void glfwPollEvents(void) { UpdatePad(); }

void glfwSetInputMode(GLFWwindow *, int, int) {}
int glfwGetKey(GLFWwindow *, int) { return GLFW_RELEASE; }
int glfwGetMouseButton(GLFWwindow *, int) { return GLFW_RELEASE; }

void
glfwGetCursorPos(GLFWwindow *, double *xpos, double *ypos)
{
	if(xpos) *xpos = 0.0;
	if(ypos) *ypos = 0.0;
}

void glfwSetCursorPos(GLFWwindow *, double, double) {}
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *, GLFWkeyfun) { return nullptr; }
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *, GLFWcursorposfun) { return nullptr; }
GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow *, GLFWcursorenterfun) { return nullptr; }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *, GLFWscrollfun) { return nullptr; }

// A single built-in "gamepad": the Vita's own controls.
int glfwJoystickPresent(int jid) { return jid == GLFW_JOYSTICK_1; }
int glfwJoystickIsGamepad(int jid) { return jid == GLFW_JOYSTICK_1; }
const char *glfwGetJoystickName(int jid) { return jid == GLFW_JOYSTICK_1 ? "PS Vita" : nullptr; }

const float *
glfwGetJoystickAxes(int jid, int *count)
{
	*count = jid == GLFW_JOYSTICK_1 ? GLFW_GAMEPAD_AXIS_LAST + 1 : 0;
	return jid == GLFW_JOYSTICK_1 ? joyAxes : nullptr;
}

const unsigned char *
glfwGetJoystickButtons(int jid, int *count)
{
	*count = jid == GLFW_JOYSTICK_1 ? GLFW_GAMEPAD_BUTTON_LAST + 1 : 0;
	return jid == GLFW_JOYSTICK_1 ? joyButtons : nullptr;
}

int
glfwGetGamepadState(int jid, GLFWgamepadstate *state)
{
	if(jid != GLFW_JOYSTICK_1)
		return GLFW_FALSE;
	*state = padState;
	return GLFW_TRUE;
}

int glfwUpdateGamepadMappings(const char *) { return GLFW_TRUE; }
GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun) { return nullptr; }

}
