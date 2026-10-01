#!/usr/bin/env python3
"""Run production save/path functions with real host files and mocked Vita I/O.

The save payload and console device are mocked; this does not run a saved game.
Exercises a fresh installation, existing files, all slots, menu discovery,
failed creation/writes/close, handle cleanup and the C stdio error flag.
Runs natively on Windows (MSVC/ASan) or Linux (GCC/ASan/UBSan).
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
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#undef _WIN32
#define PSP2
#define FIX_BUGS
#define PATH_MAX 1024
#define MAX_PATH 260
#define NUMFILES 20
#define nil nullptr
#define VITA_DATA_DIR "ux0:data/reLCS/"
#define SCE_S_ISDIR(m) ((m) == 1)
using int8 = int8_t; using int32 = int32_t; using uint8 = uint8_t; using uint32 = uint32_t; using wchar = char16_t;
#include "PCSave.h"
namespace fs = std::filesystem;
struct SceIoStat { int st_mode; };
fs::path dataRoot;
bool denyMkdir=false, denyOpen=false, failWrite=false, failClose=false, failChecksum=false;
bool setWriteStatus=true;
int openCount=0, closeCount=0, mkdirCount=0, beforeSave=0, resetCount=0;
std::vector<std::string> mkdirPaths;
struct myFILE { bool isText; FILE *file; };
myFILE myfiles[20] = {};
int myfeof(int);
int myfclose(int);
int sceIoMkdir(const char *path, int) {
    ++mkdirCount; mkdirPaths.emplace_back(path);
    assert(std::string(path).find(VITA_DATA_DIR) == 0);
    if(denyMkdir) return -1;
    std::error_code ec;
    return fs::create_directory(dataRoot/std::string(path).substr(strlen(VITA_DATA_DIR)),ec) ? 0 : -1;
}
int sceIoGetstat(const char *path, SceIoStat *s) {
    fs::path p=dataRoot/std::string(path).substr(strlen(VITA_DATA_DIR));
    if(!fs::exists(p)) return -1;
    s->st_mode=fs::is_directory(p)?1:0;
    return 0;
}
int TestChdir(const char *path) {
    if(!strcmp(path,VITA_DATA_DIR)) fs::current_path(dataRoot);
    else fs::current_path(path);
    return 0;
}
#define chdir TestChdir
const char *_psGetUserFilesFolder();
void mychdir(const char *) { assert(false && "PSP2 must not enter userfiles"); }
char DefaultPCSaveFileName[260],ValidSaveName[260];
struct CFileMgr {
    static void SetDirMyDocuments();
    static void SetDir(const char *s) { assert(!*s); ++resetCount; }
    static int OpenFile(const char *path,const char *mode) {
        if(denyOpen) { errno=EACCES; return 0; }
        for(int fd=1;fd<20;++fd) if(!myfiles[fd].file) {
            myfiles[fd].file=fopen(path,mode);
            if(myfiles[fd].file) ++openCount;
            return myfiles[fd].file?fd:0;
        }
        return 0;
    }
    static int CloseFile(int fd) {
        ++closeCount;
        const int result=myfclose(fd);
        if(failClose) { errno=ENOSPC; return EOF; }
        return result;
    }
    static size_t Read(int fd,const char *p,size_t n) { return fread((void*)p,1,n,myfiles[fd].file); }
    static size_t Write(int fd,const char *p,size_t n) {
        if(failChecksum) {
            myfiles[fd].file=freopen(ValidSaveName,"rb",myfiles[fd].file);
            assert(myfiles[fd].file);
        }
        return fwrite(p,1,n,myfiles[fd].file);
    }
    static int GetErrorReadWrite(int fd);
};
C_PcSave PcSaveHelper;
constexpr int SLOT_COUNT=8;
int Slots[SLOT_COUNT];
wchar SlotFileName[SLOT_COUNT][25],SlotSaveDate[SLOT_COUNT][80];
char TopLineEmptyFile[]="unused empty header";
struct SYSTEMTIME { uint16_t wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; };
struct SaveHeader { int size; wchar FileName[24]; SYSTEMTIME SaveDateTime; };
struct { const wchar* Get(const char*) { return u"test"; } } TheText;
struct CMessages {
    static void InsertNumberInString(const wchar*,int,int,int,int,int,int,wchar *s) { s[0]=u'!'; }
};
const char *UnicodeToAsciiForSaveLoad(const wchar*) { return "JAN"; }
void AsciiToUnicode(const char *s,wchar *out) { while((*out++=(unsigned char)*s++)); }
bool CheckDataNotCorrupt(int,char *path) {
    FILE *f=fopen(path,"rb"); if(!f) return false;
    SaveHeader h={}; bool ok=fread(&h,1,sizeof(h),f)==sizeof(h) && h.size==1234;
    fclose(f); return ok;
}
void MakeValidSaveName(int32);
void DoGameSpecificStuffBeforeSave() { ++beforeSave; }
uint32 CheckSum=12345;
struct CPad { static void FixPadsAfterSave() {} };
bool GenericSave(int file) {
    if(failWrite) {
        if(setWriteStatus) PcSaveHelper.nErrorCode=SAVESTATUS_ERR_SAVE_WRITE;
        return false;
    }
    SaveHeader h={}; h.size=1234; h.FileName[0]=u'T';
    h.SaveDateTime.wYear=2026;h.SaveDateTime.wMonth=1;h.SaveDateTime.wDay=1;
    if(fwrite(&h,1,sizeof(h),myfiles[file].file)!=sizeof(h)) return false;
    // PRODUCTION_CHECKSUM_TAIL
}
'''

TEST = r'''
void assertNoHandles() { for(auto &f:myfiles) assert(!f.file); }
void assertGameData() {
    assert(fs::current_path()==dataRoot);
    FILE *f=fopen("models/gta3.img","rb"); assert(f); fclose(f);
}
int main() {
    dataRoot=fs::current_path()/"vita-data";
    fs::create_directories(dataRoot/"models");
    FILE *model=fopen((dataRoot/"models/gta3.img").string().c_str(),"wb"); assert(model);
    fputs("fixture",model);fclose(model);
    fs::current_path(dataRoot);
    assert(!fs::exists("userfiles"));
    CFileMgr::SetDirMyDocuments();
    assert(fs::is_directory("userfiles")); assertGameData();
    assert(mkdirPaths.back()==VITA_DATA_DIR "userfiles");
    C_PcSave::SetSaveDirectory(_psGetUserFilesFolder());
    assert(!strcmp(DefaultPCSaveFileName,"userfiles/GTAVCsf"));
    // Existing directories and nested memory-card directories are safe.
    _psGetUserFilesFolder(); _psCreateFolder("userfiles\\memcard1");
    assert(fs::is_directory("userfiles/memcard1"));
    int calls=mkdirCount;
    std::string oversized(2048,'x'); _psCreateFolder(oversized.c_str());
    assert(mkdirCount==calls);
    // Every slot survives a menu reload and remains visible to PopulateSlotInfo.
    for(int slot=0;slot<SLOT_COUNT;++slot) {
        assert(PcSaveHelper.SaveSlot(slot)==0);
        assert(PcSaveHelper.nErrorCode==SAVESTATUS_SUCCESSFUL);
        assert(fs::exists("userfiles/GTAVCsf"+std::to_string(slot+1)+".b"));
        CFileMgr::SetDirMyDocuments(); assertGameData(); assertNoHandles();
    }
    PcSaveHelper.PopulateSlotInfo();
    for(int slot:Slots) assert(slot==SLOT_OK);
    assertNoHandles();
    // Missing directory that cannot be created must not report a successful save.
    fs::rename("userfiles","existing-saves");
    denyMkdir=true;
    assert(PcSaveHelper.SaveSlot(0)!=0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_CREATE);
    denyMkdir=false;
    fs::rename("existing-saves","userfiles");
    denyOpen=true;
    assert(PcSaveHelper.SaveSlot(0)!=0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_CREATE);
    denyOpen=false;
    // An ordinary file named userfiles must not be mistaken for a directory.
    fs::rename("userfiles","existing-saves");
    FILE *block=fopen("userfiles","wb"); assert(block); fclose(block);
    assert(PcSaveHelper.SaveSlot(0)!=0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_CREATE);
    fs::remove("userfiles"); fs::rename("existing-saves","userfiles");
    // Write and buffered flush/close errors both report failure and release handles.
    failWrite=true;
    for(int i=0;i<40;++i) {
        int closed=closeCount;
        assert(PcSaveHelper.SaveSlot(0)!=0);
        assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_WRITE);
        assert(closeCount==closed+1); assertNoHandles();
    }
    setWriteStatus=false;
    assert(PcSaveHelper.SaveSlot(0)!=0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_WRITE);
    failWrite=false;failChecksum=true;
    int closed=closeCount;
    assert(PcSaveHelper.SaveSlot(0)!=0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_WRITE);
    assert(closeCount==closed+1);assertNoHandles();
    failChecksum=false;failClose=true;
    assert(PcSaveHelper.SaveSlot(0)!=0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_ERR_SAVE_CLOSE);assertNoHandles();
    failClose=false;
    assert(PcSaveHelper.SaveSlot(0)==0);
    assert(PcSaveHelper.nErrorCode==SAVESTATUS_SUCCESSFUL);
    // A stdio error without EOF must be caught by the production error accessor.
    int fd=CFileMgr::OpenFile("userfiles/GTAVCsf1.b","rb"); assert(fd);
    assert(!CFileMgr::GetErrorReadWrite(fd));
    assert(fwrite("x",1,1,myfiles[fd].file)==0);
    assert(!feof(myfiles[fd].file) && ferror(myfiles[fd].file));
    assert(CFileMgr::GetErrorReadWrite(fd)); CFileMgr::CloseFile(fd);
    fd=CFileMgr::OpenFile("userfiles/GTAVCsf1.b","rb"); assert(fd);
    while(fgetc(myfiles[fd].file)!=EOF) {}
    assert(CFileMgr::GetErrorReadWrite(fd)); CFileMgr::CloseFile(fd);
    // Restore the data root even if an earlier caller changed CWD.
    fs::current_path("userfiles"); CFileMgr::SetDirMyDocuments();assertGameData();
    PcSaveHelper.PopulateSlotInfo(); for(int slot:Slots) assert(slot==SLOT_OK);
    assertNoHandles(); assert(openCount==closeCount);
    puts("PASS: fresh/existing folders, eight save slots and load-menu discovery, game-data paths, creation/write/close failures, stdio errors, handle cleanup");
}
'''


def main():
    functions = [
        ('src/skel/glfw/glfw.cpp', 'void _psCreateFolder('),
        ('src/skel/glfw/glfw.cpp', 'const char *_psGetUserFilesFolder()'),
        ('src/core/FileMgr.cpp', 'CFileMgr::SetDirMyDocuments(void)'),
        ('src/core/FileMgr.cpp', 'myfeof(int fd)'),
        ('src/core/FileMgr.cpp', 'myfclose(int fd)'),
        ('src/core/FileMgr.cpp', 'CFileMgr::GetErrorReadWrite(int fd)'),
        ('src/save/GenericGameStorage.cpp', 'MakeValidSaveName(int32 slot)'),
        ('src/save/PCSave.cpp', 'C_PcSave::SetSaveDirectory(const char *path)'),
        ('src/save/PCSave.cpp', 'C_PcSave::SaveSlot(int32 slot)'),
        ('src/save/PCSave.cpp', 'C_PcSave::PopulateSlotInfo()'),
    ]
    returns = ['', '', 'void\n', 'int\n', 'int\n', 'int\n', 'void\n', 'void\n', 'int8\n', 'void\n']
    generic = function('src/save/GenericGameStorage.cpp', 'GenericSave(int file)')
    tail = generic[generic.index('CFileMgr::Write(file, (const char *) &CheckSum'):generic.rindex('}')]
    prelude = PRELUDE.replace('// PRODUCTION_CHECKSUM_TAIL', tail)
    source = prelude + '\n'.join(ret+function(path,sig) for (path,sig),ret in zip(functions,returns)) + TEST
    with tempfile.TemporaryDirectory(prefix='relcs-save-') as tmp:
        test = Path(tmp)/'save-test.cpp'
        test.write_text(source, encoding='utf-8')
        exe, env = build(Path(tmp)/'save-test', [test], [ROOT/'src/save'])
        print(run(exe,env).splitlines()[-1])


if __name__ == '__main__':
    main()
