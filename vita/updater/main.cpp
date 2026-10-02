#include "core.h"
#include "platform.h"
#include "network.h"
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>

extern "C" {
int _newlib_heap_size_user=32*1024*1024;
unsigned int sceUserMainThreadStackSize=256*1024;
}

namespace {
unsigned WaitPress() {
    while(Update::Buttons()) sceKernelDelayThread(16000);
    for(;;) {
        unsigned buttons=Update::Buttons();
        if(buttons&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE)) return buttons;
        sceKernelDelayThread(16000);
    }
}
bool ExtractionProgress(uint64_t now,uint64_t total) {
    static uint64_t last=0;
    uint64_t time=sceKernelGetProcessTimeWide();
    if(time-last>250000) {
        Update::Screen("Preparing installation","Your current game is still installed.",int(now*100/total),"O  Cancel");
        last=time;
    }
    return !(Update::Buttons()&SCE_CTRL_CIRCLE);
}
std::string InstalledVersion() {
    // Read the installed file directly, including after app0: is released.
    FILE *file=fopen("ux0:app/RELCS0001/sce_sys/param.sfo","rb");
    if(!file) return "";
    std::vector<unsigned char> data(65536);
    size_t n=fread(data.data(),1,data.size(),file);
    bool okay=!ferror(file); fclose(file); data.resize(n);
    if(!okay) return "";
    return Update::SfoVersion(data);
}
bool Run(std::string &error) {
    using namespace Update;
    if(!Directory(Work)) { error="Cannot create update folder."; return false; }
    std::string installed=InstalledVersion();
    if(installed.empty()) { error="Cannot find the installed GTA LCS version."; return false; }
    Screen("Checking for updates","Installed version: "+installed);
    if(!InitNetwork(error)) return false;
    std::string metadata; Manifest manifest;
    if(!FetchManifest(metadata,error) || !ParseManifest(metadata,manifest,error)) return false;
    if(Version(manifest.version)<=Version(installed)) {
        Screen("You are up to date","Installed version: "+installed);
        WaitPress(); return true;
    }
    Log("Update %s -> %s",installed.c_str(),manifest.version.c_str());
    Screen("Version "+manifest.version+" is available",
        "Installed: "+installed+"\nDownload: "+std::to_string(manifest.size/(1024*1024))+" MB",-1,
        "X  Download and install       O  Back");
    if(WaitPress()&SCE_CTRL_CIRCLE) return true;
    uint64_t capacity=0,free=0;
    // Keep room for both staging and the promoter's installation transaction.
    const uint64_t required=manifest.size+2*manifest.unpacked+16*1024*1024;
    if(sceAppMgrGetDevInfo("ux0:",&capacity,&free)<0 || free<required) {
        error="Not enough free space. Free at least "+std::to_string(required/(1024*1024)+1)+" MB.";
        return false;
    }
    // Do not carry the confirmation button into transfer cancellation handling.
    while(Buttons()) sceKernelDelayThread(16000);
    if(!Download(manifest,metadata,error) || !VerifyDownload(manifest,error)) return false;
    if(!ClearPackage() || !ExtractArchive(DownloadPath,Package,manifest,Directory,ExtractionProgress,error)) {
        ClearPackage();
        if(error.empty()) error="Cannot prepare installation.";
        return false;
    }
    StopNetwork();
    Screen("Installing version "+manifest.version,"Keep the Vita powered on.",-1,"");
    // All executables, UI and libraries are in RAM and networking is closed.
    // Release the read-only app mount before replacing the installed package.
    int result=sceAppMgrUmount("app0:");
    Log("Unmount game before installation: 0x%08X",result);
    if(result<0) {
        error="Cannot release the game mount. The verified VPK is kept for retry.";
        return false;
    }
    result=Promote(Package);
    if(result<0) {
        char code[32]; snprintf(code,sizeof(code),"0x%08X",result);
        error=std::string("Installation failed: ")+code+"\nThe verified VPK is kept for retry.";
        return false;
    }
    sceIoRemove(DownloadPath);
    ClearPackage();
    // Exit so the next START gets a fresh app0: mount of the new installation.
    Screen("Update installed","Version "+manifest.version+"\nSaves and settings have been kept.\nClose this page, then select START.",-1,"O  Back");
    WaitPress();
    return true;
}
}

int main() {
    if(!Update::OpenScreen()) { sceKernelExitProcess(1); return 1; }
    Update::Log("GTA LCS Update started");
    std::string error;
    bool okay=Run(error);
    Update::StopNetwork();
    if(!okay) {
        Update::Log("Update stopped: %s",error.c_str());
        Update::Screen("Update stopped",error,-1,"O  Back");
        WaitPress();
    }
    Update::CloseScreen();
    sceKernelExitProcess(0); return 0;
}
