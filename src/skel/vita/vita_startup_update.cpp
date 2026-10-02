#ifdef PSP2
#include "vita.h"

#ifdef VITA_STARTUP_UPDATE
#include "core.h"
#include "network.h"
#include "../../../vita/updater/platform.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <psp2/message_dialog.h>
#include <vitaGL.h>

namespace {
std::atomic<bool> finished{false};
bool started=false, consumed=false;
std::string newerVersion;

std::string InstalledVersion() {
    FILE *file=fopen("app0:sce_sys/param.sfo","rb");
    if(!file) return {};
    std::vector<unsigned char> data(65536);
    size_t n=fread(data.data(),1,data.size(),file);
    bool okay=!ferror(file); fclose(file); data.resize(n);
    return okay?Update::SfoVersion(data):std::string();
}

int CheckWorker(SceSize,void*) {
    {
        const std::string installed=InstalledVersion();
        std::string error, text;
        Update::Manifest manifest;
        if(Update::Version(installed)>=0 && Update::InitNetwork(error) &&
           Update::FetchStartupManifest(text,error) && Update::ParseManifest(text,manifest,error) &&
           Update::Version(manifest.version)>Update::Version(installed)) {
            newerVersion=manifest.version;
            Update::Log("Startup update available: %s -> %s",installed.c_str(),newerVersion.c_str());
        } else if(!error.empty()) {
            Update::Log("Startup update check skipped: %s",error.c_str());
        }
        Update::StopNetwork();
    } // Release local strings before deleting the worker thread.
    // Publish only after the network and its heap allocations have been freed.
    finished.store(true,std::memory_order_release);
    return sceKernelExitDeleteThread(0);
}
}

void VitaBeginStartupUpdateCheck(void) {
    if(started) return;
    started=true;
    SceUID thread=sceKernelCreateThread("LCS update check",CheckWorker,0x10000100,
        256*1024,0,0,nullptr);
    if(thread<0) {
        Update::Log("Cannot create startup check thread: 0x%08X",thread);
        finished.store(true,std::memory_order_release); return;
    }
    int result=sceKernelStartThread(thread,0,nullptr);
    if(result<0) {
        sceKernelDeleteThread(thread);
        Update::Log("Cannot start startup check thread: 0x%08X",result);
        finished.store(true,std::memory_order_release);
    }
}

bool VitaPollStartupUpdate(void) {
    if(consumed || !finished.load(std::memory_order_acquire)) return false;
    consumed=true;
    if(newerVersion.empty()) return false;
    const char *locale=VitaGetSystemLocale();
    std::string message;
    if(locale && !strncmp(locale,"fr",2))
        message="Une mise à jour de GTA LCS est disponible : "+newerVersion+
            ".\n\nFerme le jeu, puis utilise le bouton UPDATE de son LiveArea pour l'installer.";
    else
        message="A GTA LCS update is available: "+newerVersion+
            ".\n\nClose the game, then use UPDATE in its LiveArea to install it.";
    SceMsgDialogUserMessageParam user={};
    user.buttonType=SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    user.msg=reinterpret_cast<const SceChar8*>(message.c_str());
    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode=SCE_MSG_DIALOG_MODE_USER_MSG; param.userMsgParam=&user;
    int result=sceMsgDialogInit(&param);
    if(result<0) {
        Update::Log("Cannot show startup update dialog: 0x%08X",result); return false;
    }
    while(sceMsgDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING) {
        vglSwapBuffers(GL_TRUE);
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        sceKernelDelayThread(16000);
    }
    result=sceMsgDialogTerm();
    Update::Log("Startup update dialog closed: 0x%08X",result);
    // Confirmation must not also activate a game-menu item or a cheat key.
    VitaSuppressInputUntilRelease();
    return true;
}
#else
void VitaBeginStartupUpdateCheck(void) {}
bool VitaPollStartupUpdate(void) { return false; }
#endif
#endif
