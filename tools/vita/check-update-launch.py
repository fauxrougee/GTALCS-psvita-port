#!/usr/bin/env python3
"""Check the internal updater's launch and installation ordering with Vita stubs."""
from pathlib import Path
import tempfile
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]


def stripped(path):
    return '\n'.join(line for line in path.read_text().splitlines()
                     if not line.startswith('#include'))


SOURCE = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "core.h"
#include "platform.h"
#include "network.h"
constexpr unsigned SCE_CTRL_CROSS=0x4000, SCE_CTRL_CIRCLE=0x2000;
static int mode, pressed, promotions, unmounts, opens, closes, loads;
static bool stopped;
int sceKernelDelayThread(unsigned) { return 0; }
uint64_t sceKernelGetProcessTimeWide() { return 1000000; }
int sceKernelExitProcess(int) { return 0; }
int sceIoRemove(const char*) { return 0; }
int sceAppMgrGetDevInfo(const char*,uint64_t *total,uint64_t *free) {
    *total=*free=uint64_t(1024)*1024*1024; return 0;
}
int sceAppMgrUmount(const char *path) {
    assert(!strcmp(path,"app0:") && stopped); ++unmounts;
    return mode==1?-10:0;
}
int sceAppMgrLoadExec(const char *path,void*,void*) {
    assert(!strcmp(path,"app0:updater/eboot.bin") && closes==1);
    ++loads; return mode==3?-12:0;
}
namespace Update {
bool Directory(const char*) { return true; }
bool ClearPackage() { return true; }
bool OpenScreen() { ++opens; return true; }
void CloseScreen() { ++closes; }
void Screen(const std::string&,const std::string&,int,const std::string&) {}
void Log(const char*,...) {}
unsigned Buttons() { return (++pressed%2)?0:SCE_CTRL_CROSS; }
std::string SfoVersion(const std::vector<unsigned char>&) { return "01.19"; }
int Version(const std::string &v) { return v=="01.19"?119:120; }
bool InitNetwork(std::string&) { return true; }
void StopNetwork() { stopped=true; }
bool FetchManifest(std::string&,std::string&) { return true; }
bool ParseManifest(const std::string&,Manifest &m,std::string&) {
    m.version="01.20"; m.size=1024; m.unpacked=2048; return true;
}
bool Download(const Manifest&,const std::string&,std::string&) { return true; }
bool VerifyDownload(const Manifest&,std::string&) { return true; }
bool ExtractArchive(const char*,const char*,const Manifest&,MakeDirectory,Progress,std::string&) {
    assert(!stopped && unmounts==0); return true;
}
int Promote(const char *path) {
    assert(!strcmp(path,Package) && unmounts==1 && stopped);
    ++promotions; return mode==2?-11:0;
}
}
'''


def main():
    platform = (ROOT/'vita/updater/platform.cpp').read_text()
    start = platform[platform.index('int StartUpdater()'):platform.rindex('\n}')]
    source = SOURCE+'\nnamespace Update {\n'+start+'\n}\n'
    source += stripped(ROOT/'vita/updater/main.cpp').replace('int main()', 'int UpdaterMain()')
    source += r'''
int main() {
    // The installed-version reader is real; SFO decoding is covered separately.
    for(mode=0;mode<3;++mode) {
        pressed=promotions=unmounts=0; stopped=false;
        std::string error;
        bool okay=Run(error);
        assert(unmounts==1 && stopped);
        if(mode==0) assert(okay && promotions==1 && error.empty());
        if(mode==1) assert(!okay && promotions==0 && !error.empty());
        if(mode==2) assert(!okay && promotions==1 && !error.empty());
    }
    for(mode : {0,3}) {
        opens=closes=loads=promotions=0;
        int result=Update::StartUpdater();
        assert(loads==1 && closes==1 && promotions==0);
        assert((mode==0 && result==0 && opens==0) ||
               (mode==3 && result==-12 && opens==1));
    }
    puts("PASS: internal SELF launch; no helper promotion; unmount failure blocks installation; promotion failure reported");
}
'''
    # C++ range loops require a declaration; retain the global mode for stubs.
    source = source.replace('for(mode : {0,3}) {', 'for(int testMode : {0,3}) { mode=testMode;')
    with tempfile.TemporaryDirectory(prefix='relcs-update-launch-') as folder:
        folder = Path(folder)
        fixture = folder/'installed.sfo'
        fixture.write_bytes(b'fixture')
        source = source.replace('ux0:app/RELCS0001/sce_sys/param.sfo', fixture.as_posix())
        cpp = folder/'check.cpp'
        cpp.write_text(source)
        exe, env = build(folder/'check', [cpp], [ROOT/'vita/updater'])
        print(run(exe, env))


if __name__ == '__main__':
    main()
