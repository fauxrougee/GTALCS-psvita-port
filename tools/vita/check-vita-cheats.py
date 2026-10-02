#!/usr/bin/env python3
"""Run Vita hardware translation and the production LCS cheat recognizer on host.

Effect handlers are recorded, not executed: these checks prove input/dispatch,
not weapon streaming or vehicle physics on a console.
"""
from pathlib import Path
import importlib.util
import re
import tempfile
from host_build import build, run

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('controls',ROOT/'tools/vita/check-vita-controls.py')
controls=importlib.util.module_from_spec(spec); spec.loader.exec_module(controls)

TEST=r'''
unsigned hardware(char key) {
    switch(key) {
    case 'T': return SCE_CTRL_TRIANGLE; case 'C': return SCE_CTRL_CIRCLE;
    case 'X': return SCE_CTRL_CROSS; case 'S': return SCE_CTRL_SQUARE;
    case 'U': return SCE_CTRL_UP; case 'D': return SCE_CTRL_DOWN;
    case 'L': return SCE_CTRL_LEFT; case 'R': return SCE_CTRL_RIGHT;
    case '1': return SCE_CTRL_LTRIGGER; case '3': return SCE_CTRL_RTRIGGER;
    default: assert(false); return 0;
    }
}
void poll(unsigned buttons) { input.buttons=buttons; UpdatePad(); CPad::DoCheats(); }
void reset() {
    poll(0); memset(testPad.CheatString,' ',sizeof(testPad.CheatString));
    effects=0; lastEffect.clear(); lastVehicle=-1;
    testPad.PCTempJoyState={}; // Gameplay actions deliberately remain empty.
    CPad::IsAffectedByController=false; // Physical input must still reach cheats.
}
void sequence(const char *keys,bool aliases=false) {
    for(const char *p=keys;*p;++p) {
        unsigned mask=hardware(*p);
        if(aliases && *p=='1') mask|=SCE_CTRL_L1;
        if(aliases && *p=='3') mask|=SCE_CTRL_R1;
        poll(mask);
        // A held button is a single entry, regardless of how many frames pass.
        for(int i=0;i<7;++i) poll(mask);
        poll(0);
    }
}
int main() {
    // Independent forward sequences from LCS PSP/PS2 references (not the table).
    const struct { const char *keys, *effect; } cases[]={
        {"USSDLSSR","WeaponCheat1"},{"UCCDLCCR","WeaponCheat2"},
        {"UXXDLXXR","WeaponCheat3"},{"13T13C13","MoneyCheat"},
        {"13C13X13","ArmourCheat"},{"13X13S13","HealthCheat"},
        {"13S13T13","WantedLevelUpCheat"},{"11T33XSC","WantedLevelDownCheat"},
        {"11C33STX","ExtraSunnyWeatherCheat"},{"UDCUDS13","SunnyWeatherCheat"},
        {"UDXUDT13","CloudyWeatherCheat"},{"UDSUDC13","RainyWeatherCheat"},
        {"UDTUDX13","FoggyWeatherCheat"},{"11L11RTC","VehicleCheat"},
        {"TCDTCU11","VehicleCheat"},{"11L11RCX","FastWeatherCheat"},
        {"11L11RXS","BlowUpCarsCheat"},{"11L11RST","ChangePlayerCheat"},
        {"113113LS","MayhemCheat"},{"113113UT","EverybodyAttacksPlayerCheat"},
        {"331331RC","WeaponsForAllCheat"},{"331331DX","FastTimeCheat"},
        {"3TX3SCLR","SlowTimeCheat"},{"1UL3TCDX","StrongGripCheat"},
        {"1UR3TSDX","DoShowChaseStatCheat"},{"1DL3XCUT","SuicideCheat"},
        {"TT3SS1XX","TrafficLightsCheat"},{"SS3XX1CC","MadCarsCheat"},
        {"CC3TT1SS","BlackCarsCheat"},{"CXDCXU11","BackToTheFuture"},
        {"DDDTTC13","FannyMagnetCheat"},{"1313UD13","Credits"}
    };
    for(auto &test:cases) {
        for(bool aliases:{false,true}) {
            reset(); sequence(test.keys,aliases);
            assert(effects==1 && lastEffect==test.effect);
            if(!strcmp(test.keys,"11L11RTC")) assert(lastVehicle==MI_RHINO);
            if(!strcmp(test.keys,"TCDTCU11")) assert(lastVehicle==MI_TRASH);
        }
        reset(); sequence(std::string(test.keys,7).c_str()); assert(effects==0);
        reset(); std::string wrong=test.keys; wrong[3]='T';
        if(wrong==test.keys) wrong[3]='X';
        sequence(wrong.c_str()); assert(effects==0);
    }
    reset(); sequence("13X13S13"); sequence("13X13S13");
    assert(effects==2 && lastEffect=="HealthCheat");
    reset(); sequence("TCSXUDLR"); sequence("13T13C13");
    assert(effects==1 && lastEffect=="MoneyCheat");
    reset(); sequence("31C31T31"); assert(effects==0); // Reversed money code.
    reset(); sequence("USDLSSR"); assert(effects==0); // Missing repeated square.
    reset(); sequence("USSDLSS");
    // Stick movement and touch do not enter physical cheat keys.
    input.lx=255; input.ly=0; touchHeld=true; poll(0); touchHeld=false;
    poll(SCE_CTRL_RIGHT); poll(0); assert(effects==1 && lastEffect=="WeaponCheat1");
    puts("PASS: 32 LCS PSP/PS2 codes, physical Vita L/R, alias deduplication, held/repeated keys, independent gameplay bindings, partial/wrong/reversed codes");
}
'''


def main():
    state=controls.function('src/core/Pad.h','class CControllerState')+';\n'
    source=controls.PRELUDE.replace('// PRODUCTION_CONTROLLER_STATE',state)
    source=source.replace('CControllerState PCTempJoyState={};',
        'CControllerState PCTempJoyState={}; char CheatString[12];\n'
        'void AddToCheatString(char); static void DoCheats(); void DoCheats(int16);\n'
        'static bool IsAffectedByController;')
    source='#include <string>\n#include <initializer_list>\n#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n'+source
    source=source.replace('int sceTouchPeek(SceTouchPortType,SceTouchData *p,int) { p->reportNum=0; return 1; }',
        'bool touchHeld=false;\nint sceTouchPeek(SceTouchPortType,SceTouchData *p,int) {'
        'p->reportNum=touchHeld?1:0; p->report[0].x=0; return 1; }')
    source+='\nbool CPad::IsAffectedByController; int effects,lastVehicle; std::string lastEffect;\n'
    table=(ROOT/'src/core/PadCheats.inc').read_text()
    handlers=set(re.findall(r'LCS_CHEAT\("[^"]+", (\w+)\(',table))
    for handler in sorted(handlers-{'VehicleCheat'}):
        source+=f'void {handler}() {{ ++effects; lastEffect="{handler}"; }}\n'
    source+='constexpr int MI_RHINO=101, MI_TRASH=202;\n'
    source+='void VehicleCheat(int id) { ++effects; lastEffect="VehicleCheat"; lastVehicle=id; }\n'
    source+='struct CCredits { static void Start() { ++effects; lastEffect="Credits"; } };\n'
    functions=[
        ('src/skel/vita/glfw_vita.cpp','StickAxis(unsigned char v)','float\n'),
        ('src/skel/vita/glfw_vita.cpp','ReadTouchHalves(SceTouchPortType port','void\n'),
        ('src/skel/vita/glfw_vita.cpp','UpdatePad(void)','void\n'),
        ('src/skel/vita/glfw_vita.cpp','unsigned int VitaGetCheatButtons(void)',''),
        ('src/core/Pad.cpp','void CPad::AddToCheatString(char c)',''),
        ('src/core/Pad.cpp','void CPad::DoCheats(int16 unk)',''),
        ('src/core/Pad.cpp','void CPad::DoCheats(void)','')]
    for path, signature, prefix in functions:
        source+=prefix+controls.function(path,signature)
    source+=TEST
    with tempfile.TemporaryDirectory(prefix='relcs-vita-cheats-') as temp:
        temp=Path(temp); cpp=temp/'check.cpp'; cpp.write_text(source)
        exe,env=build(temp/'check',[cpp],
            [ROOT/'src/core',ROOT/'src/skel/vita',ROOT/'vendor/librw'],
            ['PSP2','GTA_PS2_STUFF','DETECT_PAD_INPUT_SWITCH'])
        print(run(exe,env))


if __name__=='__main__': main()
