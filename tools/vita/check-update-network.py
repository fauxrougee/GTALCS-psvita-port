#!/usr/bin/env python3
"""Exercise the production transfer code with controlled HTTP responses.

On Windows, --online also checks HTTPS and GitHub redirects using Git's curl.
The fake Vita calls map the updater's own files into a temporary directory.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import tempfile
from host_build import build,run

ROOT=Path(__file__).resolve().parents[2]

MOCKS=r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "network.h"
#include "platform.h"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
struct mbedtls_sha256_context { BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; };
void mbedtls_sha256_init(mbedtls_sha256_context *c) { *c={}; }
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int) {
    return BCryptOpenAlgorithmProvider(&c->alg,BCRYPT_SHA256_ALGORITHM,nullptr,0) ||
           BCryptCreateHash(c->alg,&c->hash,nullptr,0,nullptr,0,0);
}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *data,size_t n) { return BCryptHashData(c->hash,const_cast<unsigned char*>(data),ULONG(n),0); }
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *out) { return BCryptFinishHash(c->hash,out,32,0); }
void mbedtls_sha256_free(mbedtls_sha256_context *c) { if(c->hash) BCryptDestroyHash(c->hash); if(c->alg) BCryptCloseAlgorithmProvider(c->alg,0); }
static HMODULE curlDLL=LoadLibraryA(CURL_DLL);
template<class T> T Function(const char *name) { assert(curlDLL); auto f=GetProcAddress(curlDLL,name); assert(f); return reinterpret_cast<T>(f); }
#else
#include <openssl/sha.h>
struct mbedtls_sha256_context { SHA256_CTX hash; };
void mbedtls_sha256_init(mbedtls_sha256_context*) {}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int) { return !SHA256_Init(&c->hash); }
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n) { return !SHA256_Update(&c->hash,p,n); }
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *p) { return !SHA256_Final(p,&c->hash); }
void mbedtls_sha256_free(mbedtls_sha256_context*) {}
#endif
std::string root,certificate;
bool pressed=false,corrupt=false,shortBody=false,rangeFails=false,ignoreRange=false,tlsFails=false;
int statusCode=200,requests=0;
std::vector<char> body;
bool online=false;
struct FakeCurl {
    curl_write_callback write=nullptr;
    curl_xferinfo_callback progress=nullptr;
    void *writeArg=nullptr,*progressArg=nullptr;
    curl_off_t resume=0;
    bool peer=false,host=false,protocol=false,redirectProtocol=false,ca=false,ssl=false,fail=false;
    CURL *real=nullptr;
};
// Test double records security options and injects body/range/HTTP failures.
std::map<CURL*,FakeCurl> transfers;
CURLcode TestGlobalInit(long flags) {
#ifdef _WIN32
    return Function<decltype(&curl_global_init)>("curl_global_init")(flags);
#else
    return curl_global_init(flags);
#endif
}
void TestGlobalCleanup() {
#ifdef _WIN32
    Function<decltype(&curl_global_cleanup)>("curl_global_cleanup")();
#else
    curl_global_cleanup();
#endif
}
CURL *TestInit() {
#ifdef _WIN32
    CURL *c=Function<decltype(&curl_easy_init)>("curl_easy_init")();
#else
    CURL *c=curl_easy_init();
#endif
    if(c) transfers[c].real=c; return c;
}
template<class T> CURLcode Set(CURL *c,CURLoption option,T value) {
#ifdef _WIN32
    return Function<decltype(&curl_easy_setopt)>("curl_easy_setopt")(c,option,value);
#else
    return curl_easy_setopt(c,option,value);
#endif
}
template<class T> CURLcode TestSet(CURL *c,CURLoption option,T value) {
    auto &t=transfers[c];
    if constexpr(std::is_integral<T>::value) {
        if(option==CURLOPT_SSL_VERIFYPEER) t.peer=value==1;
        if(option==CURLOPT_SSL_VERIFYHOST) t.host=value==2;
        if(option==CURLOPT_SSLVERSION) t.ssl=value==CURL_SSLVERSION_TLSv1_2;
        if(option==CURLOPT_FAILONERROR) t.fail=value==1;
        if(option==CURLOPT_RESUME_FROM_LARGE) t.resume=value;
    } else if constexpr(std::is_same<T,const char*>::value) {
        if(option==CURLOPT_PROTOCOLS_STR) t.protocol=!strcmp(value,"https");
        if(option==CURLOPT_REDIR_PROTOCOLS_STR) t.redirectProtocol=!strcmp(value,"https");
        if(option==CURLOPT_CAINFO) { t.ca=!strcmp(value,"app0:updater/certs/ca-bundle.pem"); return Set(c,option,certificate.c_str()); }
    } else if constexpr(std::is_same<T,decltype(t.write)>::value) t.write=value;
    else if constexpr(std::is_same<T,decltype(t.progress)>::value) t.progress=value;
    else if constexpr(std::is_pointer<T>::value) {
        if(option==CURLOPT_WRITEDATA) t.writeArg=value;
        if(option==CURLOPT_XFERINFODATA) t.progressArg=value;
    }
    return Set(c,option,value);
}
CURLcode TestPerform(CURL *c) {
    auto &t=transfers[c]; ++requests;
    assert(t.peer && t.host && t.ssl && t.ca && t.protocol && t.redirectProtocol && t.fail);
    assert(t.write && t.progress && t.writeArg && t.progressArg);
    if(online) {
#ifdef _WIN32
        return Function<decltype(&curl_easy_perform)>("curl_easy_perform")(c);
#else
        return curl_easy_perform(c);
#endif
    }
    if(t.progress(t.progressArg,body.size(),0,0,0)) return CURLE_ABORTED_BY_CALLBACK;
    if(tlsFails) return CURLE_PEER_FAILED_VERIFICATION;
    if(statusCode>=400) return CURLE_HTTP_RETURNED_ERROR;
    if(t.resume && rangeFails) { rangeFails=false; return CURLE_RANGE_ERROR; }
    if(t.resume && ignoreRange) return CURLE_RANGE_ERROR;
    size_t total=shortBody?body.size()-1:body.size();
    for(size_t pos=t.resume;pos<total;) {
        size_t n=std::min<size_t>(32768,total-pos);
        if(t.write(body.data()+pos,1,n,t.writeArg)!=n) return CURLE_WRITE_ERROR;
        pos+=n;
        if(t.progress(t.progressArg,body.size()-t.resume,pos-t.resume,0,0)) return CURLE_ABORTED_BY_CALLBACK;
    }
    return CURLE_OK;
}
CURLcode TestInfo(CURL *c,CURLINFO info,long *out) {
    if(online) {
#ifdef _WIN32
        return Function<decltype(&curl_easy_getinfo)>("curl_easy_getinfo")(c,info,out);
#else
        return curl_easy_getinfo(c,info,out);
#endif
    }
    assert(info==CURLINFO_RESPONSE_CODE);
    *out=statusCode>=400?statusCode:(transfers[c].resume&&!rangeFails?206:statusCode);
    return CURLE_OK;
}
void TestCleanup(CURL *c) {
#ifdef _WIN32
    Function<decltype(&curl_easy_cleanup)>("curl_easy_cleanup")(c);
#else
    curl_easy_cleanup(c);
#endif
    transfers.erase(c);
}
const curl_version_info_data *TestVersion(CURLversion version) {
#ifdef _WIN32
    return Function<decltype(&curl_version_info)>("curl_version_info")(version);
#else
    return curl_version_info(version);
#endif
}
constexpr int SCE_CTRL_CIRCLE=0x2000,SCE_SYSMODULE_NET=0;
struct SceNetInitParam { void *memory; int size,flags; };
struct SceIoStat { uint64_t st_size=0; };
std::string Rewrite(const char *path) {
    std::string s=path,base=Update::Work;
    assert(s.compare(0,base.size(),base)==0); return root+s.substr(base.size());
}
FILE *TestOpen(const char *path,const char *mode) { return fopen(Rewrite(path).c_str(),mode); }
int sceIoRemove(const char *path) { return remove(Rewrite(path).c_str()); }
int sceIoGetstat(const char *path,SceIoStat *stat) {
    std::error_code ec; auto n=std::filesystem::file_size(Rewrite(path),ec);
    if(ec) return -1; stat->st_size=n; return 0;
}
uint64_t sceKernelGetProcessTimeWide() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int sceSysmoduleLoadModule(int) { return 0; } int sceSysmoduleUnloadModule(int) { return 0; }
int sceNetInit(SceNetInitParam*) { return 0; } int sceNetTerm() { return 0; }
int sceNetCtlInit() { return 0; } int sceNetCtlTerm() { return 0; }
namespace Update {
unsigned Buttons() { return pressed?SCE_CTRL_CIRCLE:0; }
void Screen(const std::string&,const std::string&,int,const std::string&) {}
void Log(const char*,...) {}
}
#define fopen TestOpen
#define curl_global_init TestGlobalInit
#define curl_global_cleanup TestGlobalCleanup
#define curl_easy_init TestInit
#define curl_easy_setopt TestSet
#define curl_easy_perform TestPerform
#define curl_easy_getinfo TestInfo
#define curl_easy_cleanup TestCleanup
#define curl_version_info TestVersion
'''

TEST=r'''
#undef fopen
void Write(const std::string &path,const std::vector<char> &bytes) {
    std::ofstream file(path,std::ios::binary); file.write(bytes.data(),bytes.size()); assert(file.good());
}
int main(int argc,char **argv) {
    assert(argc==5); root=argv[1]; certificate=argv[2];
    std::ifstream data(argv[3],std::ios::binary); body={std::istreambuf_iterator<char>(data),{}};
    std::string sha=argv[4],error;
    assert(Update::InitNetwork(error));
    const std::string url="https://github.com/fauxrougee/GTALCS-psvita-port/releases/download/v01.19/reLCS-01.19-intro-complete.vpk";
    Update::Manifest m; m.version="01.19"; m.url=url; m.sha256=sha; m.size=body.size(); m.unpacked=body.size();
    std::string metadata="test binding";
    assert(Update::Download(m,metadata,error)); assert(Update::VerifyDownload(m,error));
    assert(std::filesystem::file_size(root+"/release.vpk.part")==body.size());
    // A complete bound file should be verified without fetching it again.
    int count=requests; assert(Update::Download(m,metadata,error)); assert(count==requests);
    // Interrupted download resumes with the recorded release identity.
    Write(root+"/release.vpk.part",{body.begin(),body.begin()+100}); error.clear();
    assert(Update::Download(m,metadata,error)); assert(Update::VerifyDownload(m,error));
    // A server that cannot resume is retried once from zero.
    Write(root+"/release.vpk.part",{body.begin(),body.begin()+100}); rangeFails=true; error.clear();
    assert(Update::Download(m,metadata,error)); assert(Update::VerifyDownload(m,error));
    // A different release binding discards stale partial bytes.
    Write(root+"/release.vpk.part",{'b','a','d'}); error.clear();
    assert(Update::Download(m,"different binding",error)); assert(Update::VerifyDownload(m,error));
    Write(root+"/release.vpk.part",{'b','a','d'}); error.clear();
    pressed=true; assert(!Update::Download(m,"different binding",error)); assert(error=="Update canceled.");
    assert(std::filesystem::file_size(root+"/release.vpk.part")==3); pressed=false;
    statusCode=404; std::string text; error.clear(); assert(!Update::FetchManifest(text,error));
    assert(error=="No updater-compatible release is available yet."); statusCode=200;
    tlsFails=true; error.clear(); assert(!Update::Download(m,"TLS test",error)); tlsFails=false;
    shortBody=true; error.clear(); assert(!Update::Download(m,"short test",error)); shortBody=false;
    // A full-sized but corrupted package is removed before any installation.
    error.clear(); assert(Update::Download(m,"integrity test",error));
    auto bad=body; bad[123]^=1; Write(root+"/release.vpk.part",bad);
    assert(!Update::VerifyDownload(m,error)); assert(!std::filesystem::exists(root+"/release.vpk.part"));
    // Oversized bodies are bounded by release metadata.
    auto bounded=m; bounded.size=1024; error.clear(); assert(!Update::Download(bounded,"bounded test",error));
    assert(std::filesystem::file_size(root+"/release.vpk.part")<=1024);
    error.clear(); assert(Update::Download(m,"cancel verify",error)); pressed=true;
    assert(!Update::VerifyDownload(m,error)); assert(std::filesystem::exists(root+"/release.vpk.part")); pressed=false;
    puts("PASS: transfer bounds, resume, cancel, HTTP/TLS errors and SHA-256 gating");
    if(getenv("RELCS_ONLINE_TEST")) {
        online=true; Update::Transfer t; long status=0; error.clear();
        assert(Update::Request("https://github.com/fauxrougee/GTALCS-psvita-port/releases/download/v01.18/SHA256SUMS.txt",t,status,error)==CURLE_OK);
        assert(status==200 && t.text.find("91f66f9e09a452d15bd361ee35238065165ab619c0b423be86e76ff1b08000f0")!=std::string::npos);
        certificate="missing-ca-bundle.pem"; Update::Transfer noCA; error.clear();
        assert(Update::Request("https://github.com/",noCA,status,error)!=CURLE_OK);
        puts("PASS: real GitHub HTTPS redirects and certificate verification (host curl)");
    }
    Update::StopNetwork(); assert(transfers.empty());
}
'''


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,default=os.environ.get('VITASDK',ROOT/'sdk/windows/vitasdk'))
    p.add_argument('--online',action='store_true')
    a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='relcs-transfer-') as folder:
        temp=Path(folder); include=temp/'include'; include.mkdir(); defines=[]
        libraries=[]
        if os.name=='nt':
            shutil.copytree(a.sdk.resolve()/'arm-vita-eabi/include/curl',include/'curl')
            dll=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Git/mingw64/bin/libcurl-4.dll'
            defines=['CURL_STATICLIB','CURL_DLL="'+dll.as_posix()+'"']; libraries=['bcrypt.lib']
        else: libraries=['-lcurl','-lcrypto']
        production=(ROOT/'vita/updater/network.cpp').read_text()
        production=re.sub(r'^#include <(?:psp2/[^>]+|mbedtls/sha256.h)>\n','',production,flags=re.M)
        source=temp/'network-test.cpp'; source.write_text(MOCKS+production+TEST)
        exe,env=build(temp/'network-test',[source],[include,ROOT/'vita/updater'],defines,libraries)
        data=bytes(range(256))*1024; fixture=temp/'body.bin'; fixture.write_bytes(data)
        work=temp/'work'; work.mkdir()
        if a.online: env['RELCS_ONLINE_TEST']='1'
        if os.name=='nt':
            key=next(k for k in env if k.lower()=='path'); env[key]=str(dll.parent)+os.pathsep+env[key]
        print(run(exe,env,[str(work),str(ROOT/'vita/updater/ca-bundle.pem'),str(fixture),hashlib.sha256(data).hexdigest()]))


if __name__=='__main__': main()
