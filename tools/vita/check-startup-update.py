#!/usr/bin/env python3
"""Check production startup notification control flow with Vita/thread stubs."""
from pathlib import Path
import tempfile
from host_build import build, run

ROOT=Path(__file__).resolve().parents[2]

MOCKS=r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core.h"
#include "network.h"
#include "platform.h"
using SceUID=int; using SceSize=unsigned; using SceChar8=char;
constexpr int SCE_MSG_DIALOG_BUTTON_TYPE_OK=0,SCE_MSG_DIALOG_MODE_USER_MSG=1;
constexpr int SCE_COMMON_DIALOG_STATUS_RUNNING=1,SCE_COMMON_DIALOG_STATUS_FINISHED=2;
constexpr int GL_TRUE=1,SCE_KERNEL_POWER_TICK_DEFAULT=0;
struct SceMsgDialogUserMessageParam { int buttonType; const char *msg; };
struct SceMsgDialogParam { int mode; SceMsgDialogUserMessageParam *userMsgParam; };
static std::string mode,message;
static int initialized,stopped,fetched,dialogs,swaps,terminated,suppressed,deleted,threadRuns;
static bool workerContext;
static int (*entry)(SceSize,void*);
void sceMsgDialogParamInit(SceMsgDialogParam *p) { *p={}; }
int sceMsgDialogInit(SceMsgDialogParam *p) {
    assert(!workerContext && stopped==1);
    assert(p->mode==SCE_MSG_DIALOG_MODE_USER_MSG);
    assert(p->userMsgParam->buttonType==SCE_MSG_DIALOG_BUTTON_TYPE_OK);
    message=p->userMsgParam->msg; ++dialogs;
    return mode=="dialog-fail"?-1:0;
}
int sceMsgDialogGetStatus() { return swaps<3?SCE_COMMON_DIALOG_STATUS_RUNNING:SCE_COMMON_DIALOG_STATUS_FINISHED; }
int sceMsgDialogTerm() { ++terminated; return 0; }
void vglSwapBuffers(int common) { assert(common==GL_TRUE && !workerContext); ++swaps; }
int sceKernelDelayThread(unsigned) { return 0; }
int sceKernelPowerTick(int) { return 0; }
int sceKernelExitDeleteThread(int) { assert(workerContext && stopped==1); return 0; }
int sceKernelCreateThread(const char*,int(*callback)(SceSize,void*),int,unsigned stack,unsigned,int,void*) {
    assert(stack==256*1024); entry=callback; return mode=="create-fail"?-1:9;
}
int sceKernelStartThread(int id,unsigned,void*) { assert(id==9); return mode=="start-fail"?-1:0; }
int sceKernelDeleteThread(int id) { assert(id==9); ++deleted; return 0; }
const char *VitaGetSystemLocale() { return mode=="english"?"en_US":"fr_FR"; }
void VitaSuppressInputUntilRelease() { ++suppressed; }
namespace Update {
void Log(const char*,...) {}
std::string SfoVersion(const std::vector<unsigned char> &data) { return data.empty()?"":"01.19"; }
int Version(const std::string &v) {
    if(v=="01.19") return 119; if(v=="01.20") return 120;
    if(v=="01.18") return 118; return -1;
}
bool InitNetwork(std::string &error) {
    assert(workerContext); ++initialized;
    if(mode=="net-fail") { error="Network failed"; return false; } return true;
}
void StopNetwork() { assert(workerContext); ++stopped; }
bool FetchStartupManifest(std::string &text,std::string &error) {
    assert(workerContext); ++fetched;
    if(mode=="offline" || mode=="http") { error="Unavailable"; return false; }
    text="manifest"; return true;
}
bool ParseManifest(const std::string&,Manifest &m,std::string &error) {
    if(mode=="invalid") { error="Invalid"; return false; }
    m.version=mode=="current"?"01.19":mode=="older"?"01.18":"01.20";
    return true;
}
}
'''

TEST=r'''
int main(int argc,char **argv) {
    assert(argc==2); mode=argv[1];
    assert(!VitaPollStartupUpdate());
    VitaBeginStartupUpdateCheck(); VitaBeginStartupUpdateCheck();
    assert(initialized==0 && dialogs==0); // Starting never waits for the worker.
    assert(!VitaPollStartupUpdate());
    if(mode=="create-fail" || mode=="start-fail") {
        assert(!VitaPollStartupUpdate() && initialized==0 && dialogs==0);
        assert(deleted==(mode=="start-fail"));
    } else {
        workerContext=true; ++threadRuns; entry(0,nullptr); workerContext=false;
        assert(stopped==1 && finished.load(std::memory_order_acquire));
        bool available=mode=="newer" || mode=="english" || mode=="dialog-fail";
        bool shown=VitaPollStartupUpdate();
        assert(dialogs==int(available));
        assert(shown==(available && mode!="dialog-fail"));
        if(shown) {
            assert(swaps==3 && terminated==1 && suppressed==1);
            assert(message.find("01.20")!=std::string::npos && message.find("UPDATE")!=std::string::npos);
            assert(message.find(mode=="english"?"A GTA LCS update":"Une mise à jour")!=std::string::npos);
        }
        assert(!VitaPollStartupUpdate() && dialogs==int(available)); // Once per launch.
        assert(initialized==(mode=="bad-sfo"?0:1));
    }
    printf("PASS startup: %s\n",mode.c_str());
}
'''


def main():
    source=(ROOT/'src/skel/vita/vita_startup_update.cpp').read_text(encoding='utf-8')
    source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
    with tempfile.TemporaryDirectory(prefix='relcs-startup-update-') as folder:
        folder=Path(folder); fixture=folder/'installed.sfo'; fixture.write_bytes(b'installed')
        source=source.replace('fopen("app0:sce_sys/param.sfo","rb")',
            'fopen(mode=="bad-sfo"?"missing.sfo":"'+fixture.as_posix()+'","rb")')
        cpp=folder/'check.cpp'; cpp.write_text(MOCKS+'\n'+source+'\n'+TEST,encoding='utf-8-sig')
        exe,env=build(folder/'check',[cpp],[ROOT/'vita/updater'],['PSP2','VITA_STARTUP_UPDATE'])
        for case in ['newer','english','current','older','offline','http','invalid','bad-sfo',
                     'net-fail','create-fail','start-fail','dialog-fail']:
            print(run(exe,env,[case]).strip())
    print('PASS: native dialog control flow; physical Vita rendering/networking still require console validation.')


if __name__=='__main__': main()
