// Minimal GLFW 3.3 API for PS Vita, implemented in glfw_vita.cpp on top of
// vitaGL and sceCtrl/sceTouch. Only what librw and the glfw skeleton use.
// Constant values match upstream GLFW 3.3.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLFW_VERSION_MAJOR 3
#define GLFW_VERSION_MINOR 3

#define GLFW_TRUE  1
#define GLFW_FALSE 0

#define GLFW_RELEASE 0
#define GLFW_PRESS   1
#define GLFW_REPEAT  2

#define GLFW_KEY_UNKNOWN       -1
#define GLFW_KEY_SPACE         32
#define GLFW_KEY_APOSTROPHE    39
#define GLFW_KEY_COMMA         44
#define GLFW_KEY_MINUS         45
#define GLFW_KEY_PERIOD        46
#define GLFW_KEY_SLASH         47
#define GLFW_KEY_0             48
#define GLFW_KEY_1             49
#define GLFW_KEY_2             50
#define GLFW_KEY_3             51
#define GLFW_KEY_4             52
#define GLFW_KEY_5             53
#define GLFW_KEY_6             54
#define GLFW_KEY_7             55
#define GLFW_KEY_8             56
#define GLFW_KEY_9             57
#define GLFW_KEY_SEMICOLON     59
#define GLFW_KEY_EQUAL         61
#define GLFW_KEY_A             65
#define GLFW_KEY_B             66
#define GLFW_KEY_C             67
#define GLFW_KEY_D             68
#define GLFW_KEY_E             69
#define GLFW_KEY_F             70
#define GLFW_KEY_G             71
#define GLFW_KEY_H             72
#define GLFW_KEY_I             73
#define GLFW_KEY_J             74
#define GLFW_KEY_K             75
#define GLFW_KEY_L             76
#define GLFW_KEY_M             77
#define GLFW_KEY_N             78
#define GLFW_KEY_O             79
#define GLFW_KEY_P             80
#define GLFW_KEY_Q             81
#define GLFW_KEY_R             82
#define GLFW_KEY_S             83
#define GLFW_KEY_T             84
#define GLFW_KEY_U             85
#define GLFW_KEY_V             86
#define GLFW_KEY_W             87
#define GLFW_KEY_X             88
#define GLFW_KEY_Y             89
#define GLFW_KEY_Z             90
#define GLFW_KEY_LEFT_BRACKET  91
#define GLFW_KEY_BACKSLASH     92
#define GLFW_KEY_RIGHT_BRACKET 93
#define GLFW_KEY_GRAVE_ACCENT  96
#define GLFW_KEY_WORLD_1       161
#define GLFW_KEY_WORLD_2       162
#define GLFW_KEY_ESCAPE        256
#define GLFW_KEY_ENTER         257
#define GLFW_KEY_TAB           258
#define GLFW_KEY_BACKSPACE     259
#define GLFW_KEY_INSERT        260
#define GLFW_KEY_DELETE        261
#define GLFW_KEY_RIGHT         262
#define GLFW_KEY_LEFT          263
#define GLFW_KEY_DOWN          264
#define GLFW_KEY_UP            265
#define GLFW_KEY_PAGE_UP       266
#define GLFW_KEY_PAGE_DOWN     267
#define GLFW_KEY_HOME          268
#define GLFW_KEY_END           269
#define GLFW_KEY_CAPS_LOCK     280
#define GLFW_KEY_SCROLL_LOCK   281
#define GLFW_KEY_NUM_LOCK      282
#define GLFW_KEY_PRINT_SCREEN  283
#define GLFW_KEY_PAUSE         284
#define GLFW_KEY_F1            290
#define GLFW_KEY_F2            291
#define GLFW_KEY_F3            292
#define GLFW_KEY_F4            293
#define GLFW_KEY_F5            294
#define GLFW_KEY_F6            295
#define GLFW_KEY_F7            296
#define GLFW_KEY_F8            297
#define GLFW_KEY_F9            298
#define GLFW_KEY_F10           299
#define GLFW_KEY_F11           300
#define GLFW_KEY_F12           301
#define GLFW_KEY_F13           302
#define GLFW_KEY_F14           303
#define GLFW_KEY_F15           304
#define GLFW_KEY_F16           305
#define GLFW_KEY_F17           306
#define GLFW_KEY_F18           307
#define GLFW_KEY_F19           308
#define GLFW_KEY_F20           309
#define GLFW_KEY_F21           310
#define GLFW_KEY_F22           311
#define GLFW_KEY_F23           312
#define GLFW_KEY_F24           313
#define GLFW_KEY_F25           314
#define GLFW_KEY_KP_0          320
#define GLFW_KEY_KP_1          321
#define GLFW_KEY_KP_2          322
#define GLFW_KEY_KP_3          323
#define GLFW_KEY_KP_4          324
#define GLFW_KEY_KP_5          325
#define GLFW_KEY_KP_6          326
#define GLFW_KEY_KP_7          327
#define GLFW_KEY_KP_8          328
#define GLFW_KEY_KP_9          329
#define GLFW_KEY_KP_DECIMAL    330
#define GLFW_KEY_KP_DIVIDE     331
#define GLFW_KEY_KP_MULTIPLY   332
#define GLFW_KEY_KP_SUBTRACT   333
#define GLFW_KEY_KP_ADD        334
#define GLFW_KEY_KP_ENTER      335
#define GLFW_KEY_KP_EQUAL      336
#define GLFW_KEY_LEFT_SHIFT    340
#define GLFW_KEY_LEFT_CONTROL  341
#define GLFW_KEY_LEFT_ALT      342
#define GLFW_KEY_LEFT_SUPER    343
#define GLFW_KEY_RIGHT_SHIFT   344
#define GLFW_KEY_RIGHT_CONTROL 345
#define GLFW_KEY_RIGHT_ALT     346
#define GLFW_KEY_RIGHT_SUPER   347
#define GLFW_KEY_MENU          348
#define GLFW_KEY_LAST          GLFW_KEY_MENU

#define GLFW_MOD_SHIFT   0x0001
#define GLFW_MOD_CONTROL 0x0002
#define GLFW_MOD_ALT     0x0004
#define GLFW_MOD_SUPER   0x0008

#define GLFW_MOUSE_BUTTON_1      0
#define GLFW_MOUSE_BUTTON_2      1
#define GLFW_MOUSE_BUTTON_3      2
#define GLFW_MOUSE_BUTTON_4      3
#define GLFW_MOUSE_BUTTON_5      4
#define GLFW_MOUSE_BUTTON_LAST   7
#define GLFW_MOUSE_BUTTON_LEFT   GLFW_MOUSE_BUTTON_1
#define GLFW_MOUSE_BUTTON_RIGHT  GLFW_MOUSE_BUTTON_2
#define GLFW_MOUSE_BUTTON_MIDDLE GLFW_MOUSE_BUTTON_3

#define GLFW_JOYSTICK_1    0
#define GLFW_JOYSTICK_LAST 15

#define GLFW_GAMEPAD_BUTTON_A            0
#define GLFW_GAMEPAD_BUTTON_B            1
#define GLFW_GAMEPAD_BUTTON_X            2
#define GLFW_GAMEPAD_BUTTON_Y            3
#define GLFW_GAMEPAD_BUTTON_LEFT_BUMPER  4
#define GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER 5
#define GLFW_GAMEPAD_BUTTON_BACK         6
#define GLFW_GAMEPAD_BUTTON_START        7
#define GLFW_GAMEPAD_BUTTON_GUIDE        8
#define GLFW_GAMEPAD_BUTTON_LEFT_THUMB   9
#define GLFW_GAMEPAD_BUTTON_RIGHT_THUMB  10
#define GLFW_GAMEPAD_BUTTON_DPAD_UP      11
#define GLFW_GAMEPAD_BUTTON_DPAD_RIGHT   12
#define GLFW_GAMEPAD_BUTTON_DPAD_DOWN    13
#define GLFW_GAMEPAD_BUTTON_DPAD_LEFT    14
#define GLFW_GAMEPAD_BUTTON_LAST         GLFW_GAMEPAD_BUTTON_DPAD_LEFT
#define GLFW_GAMEPAD_BUTTON_CROSS        GLFW_GAMEPAD_BUTTON_A
#define GLFW_GAMEPAD_BUTTON_CIRCLE       GLFW_GAMEPAD_BUTTON_B
#define GLFW_GAMEPAD_BUTTON_SQUARE       GLFW_GAMEPAD_BUTTON_X
#define GLFW_GAMEPAD_BUTTON_TRIANGLE     GLFW_GAMEPAD_BUTTON_Y

#define GLFW_GAMEPAD_AXIS_LEFT_X        0
#define GLFW_GAMEPAD_AXIS_LEFT_Y        1
#define GLFW_GAMEPAD_AXIS_RIGHT_X       2
#define GLFW_GAMEPAD_AXIS_RIGHT_Y       3
#define GLFW_GAMEPAD_AXIS_LEFT_TRIGGER  4
#define GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER 5
#define GLFW_GAMEPAD_AXIS_LAST          GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER

#define GLFW_RED_BITS              0x00021001
#define GLFW_GREEN_BITS            0x00021002
#define GLFW_BLUE_BITS             0x00021003
#define GLFW_ALPHA_BITS            0x00021004
#define GLFW_DEPTH_BITS            0x00021005
#define GLFW_STENCIL_BITS          0x00021006
#define GLFW_SAMPLES               0x0002100D
#define GLFW_REFRESH_RATE          0x0002100F
#define GLFW_CLIENT_API            0x00022001
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_OPENGL_PROFILE        0x00022008

#define GLFW_NO_API        0
#define GLFW_OPENGL_API    0x00030001
#define GLFW_OPENGL_ES_API 0x00030002

#define GLFW_CURSOR          0x00033001
#define GLFW_CURSOR_NORMAL   0x00034001
#define GLFW_CURSOR_HIDDEN   0x00034002
#define GLFW_CURSOR_DISABLED 0x00034003

#define GLFW_CONNECTED    0x00040001
#define GLFW_DISCONNECTED 0x00040002

typedef void (*GLFWglproc)(void);
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWwindow GLFWwindow;

typedef struct GLFWvidmode {
	int width;
	int height;
	int redBits;
	int greenBits;
	int blueBits;
	int refreshRate;
} GLFWvidmode;

typedef struct GLFWgamepadstate {
	unsigned char buttons[15];
	float axes[6];
} GLFWgamepadstate;

typedef void (*GLFWerrorfun)(int, const char *);
typedef void (*GLFWwindowfocusfun)(GLFWwindow *, int);
typedef void (*GLFWwindowiconifyfun)(GLFWwindow *, int);
typedef void (*GLFWframebuffersizefun)(GLFWwindow *, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow *, double, double);
typedef void (*GLFWcursorenterfun)(GLFWwindow *, int);
typedef void (*GLFWscrollfun)(GLFWwindow *, double, double);
typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWjoystickfun)(int, int);

int glfwInit(void);
void glfwTerminate(void);
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback);
double glfwGetTime(void);

GLFWmonitor **glfwGetMonitors(int *count);
GLFWmonitor *glfwGetPrimaryMonitor(void);
const char *glfwGetMonitorName(GLFWmonitor *monitor);
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *monitor, int *count);
const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor);

void glfwWindowHint(int hint, int value);
GLFWwindow *glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share);
void glfwDestroyWindow(GLFWwindow *window);
int glfwWindowShouldClose(GLFWwindow *window);
void glfwSetWindowShouldClose(GLFWwindow *window, int value);
void glfwGetWindowSize(GLFWwindow *window, int *width, int *height);
void glfwSetWindowSize(GLFWwindow *window, int width, int height);
void glfwSetWindowPos(GLFWwindow *window, int xpos, int ypos);
void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height);
GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow *window, GLFWwindowfocusfun callback);
GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow *window, GLFWwindowiconifyfun callback);
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow *window, GLFWframebuffersizefun callback);

void glfwMakeContextCurrent(GLFWwindow *window);
void glfwSwapBuffers(GLFWwindow *window);
void glfwSwapInterval(int interval);
GLFWglproc glfwGetProcAddress(const char *procname);

void glfwPollEvents(void);
void glfwSetInputMode(GLFWwindow *window, int mode, int value);
int glfwGetKey(GLFWwindow *window, int key);
int glfwGetMouseButton(GLFWwindow *window, int button);
void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos);
void glfwSetCursorPos(GLFWwindow *window, double xpos, double ypos);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun callback);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *window, GLFWcursorposfun callback);
GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow *window, GLFWcursorenterfun callback);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun callback);

int glfwJoystickPresent(int jid);
int glfwJoystickIsGamepad(int jid);
const char *glfwGetJoystickName(int jid);
const float *glfwGetJoystickAxes(int jid, int *count);
const unsigned char *glfwGetJoystickButtons(int jid, int *count);
int glfwGetGamepadState(int jid, GLFWgamepadstate *state);
int glfwUpdateGamepadMappings(const char *string);
GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun callback);

#ifdef __cplusplus
}
#endif
