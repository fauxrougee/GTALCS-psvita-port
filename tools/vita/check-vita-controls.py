#!/usr/bin/env python3
"""Exercise production Vita input and GTA bindings with controlled hardware reads.

Runs native host C++ with sanitizers. Checks fresh and saved binding tables,
including old initialization counts, custom bindings, menu input and release.
This tests input translation, not physical buttons or gameplay on a console.
"""
from pathlib import Path
import tempfile
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]


def function(path, signature):
    text = (ROOT/path).read_text(encoding='utf-8')
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


PRELUDE = r'''
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <cstring>
#define RW_GL3
#define LOAD_INI_SETTINGS
#define rsNULL 0
using int8=int8_t; using int16=int16_t; using int32=int32_t;
using uint8=uint8_t; using uint16=uint16_t; using uint32=uint32_t;
using wchar=char16_t; using RsKeyCodes=int;
int Min(int a,int b) { return a<b?a:b; }
#include "GLFW/glfw3.h"
#include "ControllerConfig.h"
// PRODUCTION_CONTROLLER_STATE
class CPad {
public:
    CControllerState PCTempJoyState={};
    static CPad *GetPad(int);
};
CPad testPad;
CPad *CPad::GetPad(int) { return &testPad; }
CControllerConfigManager::CControllerConfigManager() {
    m_bFirstCapture=false;
    memset(&m_NewState,0,sizeof(m_NewState));
    memset(&m_OldState,0,sizeof(m_OldState));
    memset(m_aButtonStates,0,sizeof(m_aButtonStates));
    memset(m_aSimCheckers,0,sizeof(m_aSimCheckers));
}
uint32 CControllerConfigManager::ms_padButtonsInited=0;
// Masks from VitaSDK psp2common/ctrl.h; the host does not need an installed SDK.
enum {
    SCE_CTRL_SELECT=1, SCE_CTRL_L3=2, SCE_CTRL_R3=4, SCE_CTRL_START=8,
    SCE_CTRL_UP=0x10, SCE_CTRL_RIGHT=0x20, SCE_CTRL_DOWN=0x40, SCE_CTRL_LEFT=0x80,
    SCE_CTRL_LTRIGGER=0x100, SCE_CTRL_L2=SCE_CTRL_LTRIGGER,
    SCE_CTRL_RTRIGGER=0x200, SCE_CTRL_R2=SCE_CTRL_RTRIGGER,
    SCE_CTRL_L1=0x400, SCE_CTRL_R1=0x800, SCE_CTRL_TRIANGLE=0x1000,
    SCE_CTRL_CIRCLE=0x2000, SCE_CTRL_CROSS=0x4000, SCE_CTRL_SQUARE=0x8000
};
struct SceCtrlData { unsigned buttons=0; unsigned char lx=128,ly=128,rx=128,ry=128; };
SceCtrlData input;
using SceTouchPortType=int;
struct SceTouchData { int reportNum; struct { int x; } report[6]; };
constexpr int SCE_TOUCH_PORT_FRONT=0,SCE_TOUCH_PORT_BACK=1;
int sceCtrlPeekBufferPositiveExt2(int,SceCtrlData *p,int) { *p=input; return 1; }
int sceTouchPeek(SceTouchPortType,SceTouchData *p,int) { p->reportNum=0; return 1; }
#define FRONT_TOUCH_WIDTH 1920
#define REAR_TOUCH_WIDTH 1920
GLFWgamepadstate padState={};
unsigned char joyButtons[GLFW_GAMEPAD_BUTTON_LAST+1];
float joyAxes[GLFW_GAMEPAD_AXIS_LAST+1];
'''

TEST = r'''
void capture(CControllerConfigManager &controls,unsigned buttons) {
    input.buttons=buttons; UpdatePad();
    controls.m_NewState.isGamepad=true;
    memcpy(controls.m_NewState.mappedButtons,padState.buttons,sizeof(padState.buttons));
    controls.m_NewState.mappedButtons[15]=padState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]>-0.8f;
    controls.m_NewState.mappedButtons[16]=padState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]>-0.8f;
    controls.UpdateJoyButtonState(0);
}
void horizontal(CControllerConfigManager &controls,unsigned buttons,int left,int right) {
    capture(controls,buttons);
    CControllerState state={};
    memset(controls.m_aSimCheckers,0,sizeof(controls.m_aSimCheckers));
    for(int i=0;i<16;++i)
        if(controls.m_aButtonStates[i])
            controls.AffectControllerStateOn_ButtonDown_AllStates(i+1,JOYSTICK,state);
    assert(state.DPadLeft==left && state.DPadRight==right);
}
void fresh(int count) {
    CControllerConfigManager::ms_padButtonsInited=0;
    CControllerConfigManager controls;
    controls.InitDefaultControlConfigJoyPad(count);
#ifdef PSP2
    assert(CControllerConfigManager::ms_padButtonsInited==16);
    assert(controls.GetControllerKeyAssociatedWithAction(GO_LEFT,JOYSTICK)==16);
    horizontal(controls,SCE_CTRL_LEFT,255,0);
    horizontal(controls,0,0,0);
    horizontal(controls,SCE_CTRL_RIGHT,0,255);
    horizontal(controls,SCE_CTRL_LEFT|SCE_CTRL_RIGHT,0,0);
    // Repeated initialization is idempotent.
    auto before=controls;
    for(int i=0;i<10;++i) controls.InitDefaultControlConfigJoyPad(count);
    assert(!memcmp(before.m_aSettings,controls.m_aSettings,sizeof(controls.m_aSettings)));
#else
    // Desktop initialization still uses the reported count.
    assert(CControllerConfigManager::ms_padButtonsInited==(unsigned)count);
    assert(controls.GetControllerKeyAssociatedWithAction(GO_LEFT,JOYSTICK)==(count==16?16:0));
    horizontal(controls,SCE_CTRL_LEFT,count==16?255:0,0);
#endif
    assert(controls.GetControllerKeyAssociatedWithAction(GO_RIGHT,JOYSTICK)==14);
    assert(controls.GetControllerKeyAssociatedWithAction(GO_FORWARD,JOYSTICK)==13);
    assert(controls.GetControllerKeyAssociatedWithAction(GO_BACK,JOYSTICK)==15);
    // All directions reach the engine and the existing menu handlers.
    unsigned masks[]={SCE_CTRL_UP,SCE_CTRL_RIGHT,SCE_CTRL_DOWN,SCE_CTRL_LEFT};
    int maps[]={GLFW_GAMEPAD_BUTTON_DPAD_UP,GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,
                GLFW_GAMEPAD_BUTTON_DPAD_DOWN,GLFW_GAMEPAD_BUTTON_DPAD_LEFT};
    int ids[]={13,14,15,16};
    int16 CControllerState::*members[]={&CControllerState::DPadUp,&CControllerState::DPadRight,
                                      &CControllerState::DPadDown,&CControllerState::DPadLeft};
    for(int d=0;d<4;++d) {
        capture(controls,masks[d]);
        assert(padState.buttons[maps[d]]==1 && MapIdToButtonId(maps[d])==ids[d]);
        assert(controls.m_aButtonStates[ids[d]-1]);
        testPad.PCTempJoyState={};
        controls.UpdateJoyInConfigMenus_ButtonDown(ids[d],0);
        assert(testPad.PCTempJoyState.*members[d]==255);
        controls.UpdateJoyInConfigMenus_ButtonUp(ids[d],0);
        assert(testPad.PCTempJoyState.*members[d]==0);
        capture(controls,0);
        assert(!controls.m_aButtonStates[ids[d]-1]);
    }
    capture(controls,SCE_CTRL_L2|SCE_CTRL_R2);
    assert(controls.m_aButtonStates[4] && controls.m_aButtonStates[5]);
}
#ifdef PSP2
void legacy(unsigned initialized,bool customLeft) {
    // Reproduce the tables loaded from older .set/.ini files. Keep every other
    // entry distinct to detect any accidental reset of existing user controls.
    CControllerConfigManager controls;
    for(int a=0;a<MAX_CONTROLLERACTIONS;++a)
        for(int t=0;t<MAX_CONTROLLERTYPES;++t) {
            controls.m_aSettings[a][t].m_Key=100+a*4+t;
            controls.m_aSettings[a][t].m_ContSetOrder=t+1;
        }
    controls.m_aSettings[GO_LEFT][JOYSTICK].m_Key=customLeft?9:0;
    controls.m_aSettings[GO_LEFT][JOYSTICK].m_ContSetOrder=customLeft?4:0;
    auto before=controls;
    CControllerConfigManager::ms_padButtonsInited=initialized;
    controls.InitDefaultControlConfigJoyPad(15);
    assert(CControllerConfigManager::ms_padButtonsInited==16);
    assert(controls.GetControllerKeyAssociatedWithAction(GO_LEFT,JOYSTICK)==(customLeft?9:16));
    for(int a=0;a<MAX_CONTROLLERACTIONS;++a)
        assert(!memcmp(before.m_aSettings[a],controls.m_aSettings[a],sizeof(controls.m_aSettings[a]))
               || (!customLeft && a==GO_LEFT));
    for(int t=0;t<JOYSTICK;++t)
        assert(!memcmp(&before.m_aSettings[GO_LEFT][t],&controls.m_aSettings[GO_LEFT][t],sizeof(controls.m_aSettings[GO_LEFT][t])));
    auto repaired=controls;
    controls.InitDefaultControlConfigJoyPad(15);
    assert(!memcmp(repaired.m_aSettings,controls.m_aSettings,sizeof(controls.m_aSettings)));
    if(customLeft) horizontal(controls,SCE_CTRL_SELECT,255,0);
    else horizontal(controls,SCE_CTRL_LEFT,255,0);
}
#endif
int main() {
    int count=0; glfwGetJoystickButtons(GLFW_JOYSTICK_1,&count);
    assert(count==15); // Physical GLFW arrays must retain their real length.
    fresh(count); fresh(16);
    int missing=-1; assert(glfwGetJoystickButtons(-1,&missing)==nullptr && missing==0);
    CControllerConfigManager::ms_padButtonsInited=0;
    CControllerConfigManager disconnected; disconnected.InitDefaultControlConfigJoyPad(0);
    assert(disconnected.GetControllerKeyAssociatedWithAction(GO_LEFT,JOYSTICK)==0);
#ifdef PSP2
    legacy(15,false); legacy(16,false); legacy(15,true); legacy(16,true);
    puts("PASS: Vita fresh and legacy bindings, custom controls preserved, D-pad presses/releases, menu input, trigger IDs and repeated initialization");
#else
    puts("PASS: desktop reported button count and initialization preserved");
#endif
}
'''


def main():
    state = function('src/core/Pad.h','class CControllerState') + ';\n'
    source = PRELUDE.replace('// PRODUCTION_CONTROLLER_STATE',state)
    functions = [
        ('src/skel/vita/glfw_vita.cpp','StickAxis(unsigned char v)','float\n'),
        ('src/skel/vita/glfw_vita.cpp','ReadTouchHalves(SceTouchPortType port','void\n'),
        ('src/skel/vita/glfw_vita.cpp','UpdatePad(void)','void\n'),
        ('src/skel/vita/glfw_vita.cpp','glfwGetJoystickButtons(int jid, int *count)','const unsigned char *\n'),
        ('src/core/ControllerConfig.cpp','int MapIdToButtonId(int mapId)',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::InitDefaultControlConfigJoyPad(uint32 buttons)',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::UpdateJoyButtonState(int32 padnumber)',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::AffectControllerStateOn_ButtonDown_AllStates(int32 button',''),
        ('src/core/ControllerConfig.cpp','int32 CControllerConfigManager::GetControllerKeyAssociatedWithAction(e_ControllerAction action',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::SetControllerKeyAssociatedWithAction(e_ControllerAction action',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::ResetSettingOrder(e_ControllerAction action)',''),
        ('src/core/ControllerConfig.cpp','int32 CControllerConfigManager::GetNumOfSettingsForAction(e_ControllerAction action)',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::UpdateJoyInConfigMenus_ButtonDown(int32 button',''),
        ('src/core/ControllerConfig.cpp','void CControllerConfigManager::UpdateJoyInConfigMenus_ButtonUp(int32 button',''),
    ]
    source += '\n'.join(ret+function(path,sig) for path,sig,ret in functions) + TEST
    with tempfile.TemporaryDirectory(prefix='relcs-controls-') as tmp:
        test = Path(tmp)/'controls-test.cpp'
        test.write_text(source,encoding='utf-8')
        for name,defines in [('vita',['PSP2']),('desktop',[])]:
            exe,env = build(Path(tmp)/name,[test],[ROOT/'src/core',ROOT/'src/skel/vita'],defines)
            print(run(exe,env).strip())


if __name__ == '__main__':
    main()
