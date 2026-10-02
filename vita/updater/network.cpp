#include "network.h"
#include "platform.h"
#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Update {
namespace {
bool netModule=false, netReady=false, ctlReady=false, curlReady=false;
std::vector<unsigned char> netMemory;
struct Transfer {
    FILE *file=nullptr;
    std::string text;
    uint64_t base=0, limit=4096, received=0, lastUI=0;
    bool canceled=false;
    bool quiet=false;
    std::string label="Checking for updates";
};
size_t Receive(char *data,size_t size,size_t count,void *arg) {
    auto &t=*static_cast<Transfer*>(arg);
    if(size && count>SIZE_MAX/size) return 0;
    size_t length=size*count;
    if(length>t.limit-t.base-t.received) return 0;
    if(t.file) { if(fwrite(data,1,length,t.file)!=length) return 0; }
    else t.text.append(data,length);
    t.received+=length;
    return length;
}
int Progress(void *arg,curl_off_t,curl_off_t now,curl_off_t,curl_off_t) {
    auto &t=*static_cast<Transfer*>(arg);
    if(t.quiet) return 0;
    if(Buttons()&SCE_CTRL_CIRCLE) { t.canceled=true; return 1; }
    uint64_t clock=sceKernelGetProcessTimeWide();
    if(clock-t.lastUI>=250000) {
        uint64_t done=t.base+std::max<curl_off_t>(now,0);
        int percent=t.file?int(std::min<uint64_t>(done,t.limit)*100/t.limit):-1;
        Screen(t.label,t.file?std::to_string(done/(1024*1024))+" / "+
               std::to_string(t.limit/(1024*1024))+" MB":"Connect your Vita to Wi-Fi.",percent,"O  Cancel");
        t.lastUI=clock;
    }
    return 0;
}
CURLcode Request(const char *url,Transfer &transfer,long &status,std::string &error) {
    CURL *curl=curl_easy_init();
    if(!curl) { error="Cannot start HTTPS connection."; return CURLE_FAILED_INIT; }
    char message[CURL_ERROR_SIZE]={};
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"reLCS-PSVita-Updater/1");
    curl_easy_setopt(curl,CURLOPT_CAINFO,"app0:updater/certs/ca-bundle.pem");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_SSLVERSION,long(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
    if(transfer.quiet) {
        curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,2000L);
        curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,6000L);
    }
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(curl,CURLOPT_LOW_SPEED_TIME,30L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_MAXFILESIZE_LARGE,curl_off_t(transfer.limit-transfer.base));
    curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,message);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,Receive);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&transfer);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,Progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&transfer);
    if(transfer.base) curl_easy_setopt(curl,CURLOPT_RESUME_FROM_LARGE,curl_off_t(transfer.base));
    CURLcode result=curl_easy_perform(curl);
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    if(result!=CURLE_OK) {
        error=transfer.canceled?"Update canceled.":"Download failed. Check Wi-Fi and the system date.";
        Log("HTTPS error %d HTTP %ld: %s",int(result),status,message);
    }
    curl_easy_cleanup(curl);
    return result;
}
bool ReadText(const char *path,std::string &text) {
    FILE *file=fopen(path,"rb"); if(!file) return false;
    char data[4097]; size_t n=fread(data,1,sizeof(data),file);
    bool okay=n<=4096 && !ferror(file); fclose(file);
    if(okay) text.assign(data,n);
    return okay;
}
}
bool InitNetwork(std::string &error) {
    netModule=sceSysmoduleLoadModule(SCE_SYSMODULE_NET)>=0;
    if(netModule) {
        netMemory.resize(1024*1024);
        SceNetInitParam init={netMemory.data(),int(netMemory.size()),0};
        netReady=sceNetInit(&init)>=0;
    }
    if(netReady) ctlReady=sceNetCtlInit()>=0;
    if(ctlReady) curlReady=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    if(!curlReady) { error="Cannot initialize networking."; StopNetwork(); return false; }
    const auto *version=curl_version_info(CURLVERSION_NOW);
    Log("Network: curl %s, TLS %s",version->version,version->ssl_version);
    return true;
}
void StopNetwork() {
    if(curlReady) curl_global_cleanup();
    if(ctlReady) sceNetCtlTerm();
    if(netReady) sceNetTerm();
    if(netModule) sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    curlReady=ctlReady=netReady=netModule=false;
    std::vector<unsigned char>().swap(netMemory);
}
bool FetchStartupManifest(std::string &text,std::string &error) {
    int state=0;
    if(sceNetCtlInetGetState(&state)<0 || state!=SCE_NETCTL_STATE_CONNECTED) {
        error="Offline; startup update check skipped."; return false;
    }
    Transfer transfer; transfer.quiet=true; long status=0;
    CURLcode result=Request(MetadataURL,transfer,status,error);
    if(result!=CURLE_OK || status!=200) return false;
    text=transfer.text; return true;
}
bool FetchManifest(std::string &text,std::string &error) {
    Transfer transfer; long status=0;
    CURLcode result=Request(MetadataURL,transfer,status,error);
    if(status==404) error="No updater-compatible release is available yet.";
    if(result!=CURLE_OK || status!=200) return false;
    text=transfer.text; return true;
}
bool Download(const Manifest &m,const std::string &metadata,std::string &error) {
    const std::string binding=std::string(Work)+"/download.txt";
    std::string previous;
    if(!ReadText(binding.c_str(),previous) || previous!=metadata) {
        sceIoRemove(DownloadPath);
        FILE *file=fopen(binding.c_str(),"wb");
        if(!file) { error="Cannot store download information."; return false; }
        bool okay=fwrite(metadata.data(),1,metadata.size(),file)==metadata.size();
        if(fclose(file)!=0) okay=false;
        if(!okay) { error="Cannot store download information."; return false; }
    }
    for(unsigned attempt=0;attempt<2;++attempt) {
        SceIoStat stat={}; uint64_t base=0;
        if(sceIoGetstat(DownloadPath,&stat)>=0) base=stat.st_size;
        if(base>m.size) { sceIoRemove(DownloadPath); base=0; }
        if(base==m.size) return true;
        Transfer transfer; transfer.base=base; transfer.limit=m.size;
        transfer.label=base?"Resuming download":"Downloading update";
        transfer.file=fopen(DownloadPath,base?"ab":"wb");
        if(!transfer.file) { error="Cannot write the download. Check free space."; return false; }
        long status=0;
        CURLcode result=Request(m.url.c_str(),transfer,status,error);
        bool closed=fclose(transfer.file)==0;
        if(base && result==CURLE_RANGE_ERROR && !transfer.canceled) {
            sceIoRemove(DownloadPath); error.clear(); continue;
        }
        if(result!=CURLE_OK || !closed || status!=(base?206:200) || base+transfer.received!=m.size) {
            if(error.empty()) error="Download is incomplete. Try Update again to resume.";
            return false;
        }
        return true;
    }
    error="The server cannot resume this download."; return false;
}
bool VerifyDownload(const Manifest &m,std::string &error) {
    FILE *file=fopen(DownloadPath,"rb");
    if(!file) { error="Cannot read downloaded package."; return false; }
    mbedtls_sha256_context hash; mbedtls_sha256_init(&hash);
    bool okay=mbedtls_sha256_starts(&hash,0)==0;
    unsigned char data[32768], digest[32]; size_t count; uint64_t total=0,last=0;
    while(okay && (count=fread(data,1,sizeof(data),file))>0) {
        okay=mbedtls_sha256_update(&hash,data,count)==0; total+=count;
        if(Buttons()&SCE_CTRL_CIRCLE) { okay=false; error="Update canceled."; break; }
        uint64_t time=sceKernelGetProcessTimeWide();
        if(time-last>250000) { Screen("Verifying download","Checking SHA-256...",int(total*100/m.size),"O  Cancel"); last=time; }
    }
    if(ferror(file)) okay=false;
    if(fclose(file)) okay=false;
    if(okay) okay=mbedtls_sha256_finish(&hash,digest)==0;
    mbedtls_sha256_free(&hash);
    std::string actual;
    if(okay) for(unsigned char byte:digest) {
        const char *hex="0123456789abcdef"; actual+=hex[byte>>4]; actual+=hex[byte&15];
    }
    if(!okay || total!=m.size || actual!=m.sha256) {
        if(error.empty()) { error="Package checksum failed. Download it again."; sceIoRemove(DownloadPath); }
        return false;
    }
    Log("Verified release %s: %llu bytes, SHA256 %s",m.version.c_str(),
        static_cast<unsigned long long>(total),actual.c_str());
    return true;
}
}
