#include "platform.h"
#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Update {
bool Directory(const char *path) {
    if(sceIoMkdir(path,0777)>=0) return true;
    SceIoStat stat={};
    return sceIoGetstat(path,&stat)>=0 && SCE_S_ISDIR(stat.st_mode);
}
namespace {
bool RemoveTree(const std::string &path, unsigned depth=0) {
    // Only this updater's staging tree may be removed, never the game's data.
    if(path.compare(0,strlen(Package),Package) ||
       (path.size()>strlen(Package) && path[strlen(Package)]!='/') || depth>12) return false;
    SceIoStat stat={};
    if(sceIoGetstat(path.c_str(),&stat)<0) return true;
    if(!SCE_S_ISDIR(stat.st_mode)) return sceIoRemove(path.c_str())>=0;
    int dir=sceIoDopen(path.c_str());
    if(dir<0) return false;
    bool okay=true; int result;
    SceIoDirent entry={};
    while((result=sceIoDread(dir,&entry))>0) {
        if(strcmp(entry.d_name,".") && strcmp(entry.d_name,".."))
            if(!RemoveTree(path+"/"+entry.d_name,depth+1)) { okay=false; break; }
        entry={};
    }
    if(result<0) okay=false;
    if(sceIoDclose(dir)<0) okay=false;
    return okay && sceIoRmdir(path.c_str())>=0;
}
}
bool ClearPackage() { return RemoveTree(Package) && Directory(Package); }
void Log(const char *format,...) {
    Directory("ux0:data/reLCS");
    FILE *file=fopen("ux0:data/reLCS/update.log","a");
    if(!file) return;
    va_list args; va_start(args,format); vfprintf(file,format,args); va_end(args);
    fputc('\n',file); fclose(file);
}
int Promote(const char *path) {
    // The SDK's internal PAF and promoter modules implement package installation.
    uint32_t args[]={0x180000,0xffffffff,0xffffffff,1,0xffffffff,0xffffffff};
    int pafResult=0;
    SceSysmoduleOpt opt={sizeof(SceSysmoduleOpt),&pafResult,{-1,-1}};
    int result=sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,
        sizeof(args),args,&opt);
    if(result<0) return result;
    if(pafResult<0) result=pafResult;
    bool module=false, initialized=false;
    if(result>=0) {
        result=sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        module=result>=0;
    }
    if(result>=0) { result=scePromoterUtilityInit(); initialized=result>=0; }
    if(result>=0) result=scePromoterUtilityPromotePkgWithRif(path,1);
    if(initialized) scePromoterUtilityExit();
    if(module) sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    SceSysmoduleOpt unload={};
    sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,0,nullptr,&unload);
    Log("Promote %s: 0x%08X",path,result);
    return result;
}
int StartUpdater() {
    // Replace this process within the game title; never register another app.
    CloseScreen();
    int result=sceAppMgrLoadExec("app0:updater/eboot.bin",nullptr,nullptr);
    Log("Load internal updater: 0x%08X",result);
    if(result<0) OpenScreen();
    return result;
}
}
