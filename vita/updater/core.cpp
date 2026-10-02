#include "core.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <zlib.h>

namespace Update {
namespace {
uint16_t U16(const unsigned char *p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
uint32_t U32(const unsigned char *p) { return uint32_t(U16(p)) | uint32_t(U16(p+2))<<16; }
bool Fail(std::string &error, const char *message) { error=message; return false; }
bool Number(const std::string &s, uint64_t &n) {
    n=0;
    if(s.empty() || s.size()>10) return false;
    for(char c:s) { if(c<'0'||c>'9') return false; n=n*10+unsigned(c-'0'); }
    return true;
}
bool Read(FILE *f, void *p, size_t n) { return fread(p,1,n,f)==n; }
struct File {
    FILE *f;
    explicit File(const char *p, const char *mode):f(fopen(p,mode)) {}
    ~File() { if(f) fclose(f); }
};
bool Archive(FILE *f, const Manifest &m, std::vector<Entry> &entries, std::string &error) {
    entries.clear();
    if(fseek(f,0,SEEK_END)!=0 || ftell(f)!=long(m.size) || m.size<22)
        return Fail(error,"Package size does not match the release.");
    const size_t tailSize=std::min<uint64_t>(m.size,65557);
    std::vector<unsigned char> tail(tailSize);
    if(fseek(f,long(m.size-tailSize),SEEK_SET)!=0 || !Read(f,tail.data(),tailSize))
        return Fail(error,"Cannot read package directory.");
    size_t end=tailSize;
    for(size_t pos=tailSize-22;;--pos) {
        if(U32(&tail[pos])==0x06054b50 && pos+22+U16(&tail[pos+20])==tailSize) { end=pos; break; }
        if(pos==0) break;
    }
    if(end==tailSize) return Fail(error,"Invalid ZIP directory.");
    auto e=&tail[end];
    uint32_t centralSize=U32(e+12), central=U32(e+16);
    uint16_t count=U16(e+10);
    if(U16(e+4)||U16(e+6)||U16(e+8)!=count||!count||count>128 || centralSize>65536 ||
       uint64_t(central)+centralSize!=m.size-tailSize+end)
        return Fail(error,"Unsupported ZIP directory.");
    std::vector<unsigned char> directory(centralSize);
    if(fseek(f,central,SEEK_SET)!=0 || !Read(f,directory.data(),directory.size()))
        return Fail(error,"Cannot read ZIP entries.");
    std::set<std::string> paths;
    std::vector<std::pair<uint64_t,uint64_t>> ranges;
    size_t pos=0; uint64_t total=0;
    for(unsigned i=0;i<count;++i) {
        if(pos+46>directory.size() || U32(&directory[pos])!=0x02014b50)
            return Fail(error,"Invalid ZIP entry.");
        const auto p=&directory[pos];
        uint16_t length=U16(p+28), extra=U16(p+30), comment=U16(p+32);
        if(pos+46+length+extra+comment>directory.size() || !length || length>180 || U16(p+34))
            return Fail(error,"Invalid ZIP entry size.");
        Entry item;
        item.name.assign(reinterpret_cast<const char*>(p+46),length);
        item.flags=U16(p+8); item.method=U16(p+10); item.crc=U32(p+16);
        item.packed=U32(p+20); item.unpacked=U32(p+24); item.offset=U32(p+42);
        if(!SafePath(item.name) || !paths.insert(item.name).second || (item.flags&~0x808U) ||
           (item.method!=0 && item.method!=8) || ((U32(p+38)>>16)&0170000)==0120000 ||
           uint64_t(item.offset)+30+length+item.packed>central || item.unpacked>512*1024*1024U)
            return Fail(error,"Unsafe or unsupported ZIP entry.");
        unsigned char local[30];
        if(fseek(f,item.offset,SEEK_SET)!=0 || !Read(f,local,sizeof(local)) ||
           U32(local)!=0x04034b50 || U16(local+6)!=item.flags || U16(local+8)!=item.method ||
           U16(local+26)!=length)
            return Fail(error,"ZIP headers disagree.");
        std::vector<char> name(length);
        if(!Read(f,name.data(),length) || memcmp(name.data(),item.name.data(),length))
            return Fail(error,"ZIP paths disagree.");
        uint64_t data=uint64_t(item.offset)+30+length+U16(local+28);
        if(data+item.packed>central || (!(item.flags&8) &&
           (U32(local+14)!=item.crc || U32(local+18)!=item.packed || U32(local+22)!=item.unpacked)))
            return Fail(error,"Invalid ZIP data range.");
        ranges.emplace_back(item.offset,data+item.packed);
        item.offset=uint32_t(data);
        total+=item.unpacked;
        if(total>m.unpacked) return Fail(error,"Package exceeds the release size.");
        entries.push_back(item); pos+=46+length+extra+comment;
    }
    std::sort(ranges.begin(),ranges.end());
    for(size_t i=1;i<ranges.size();++i)
        if(ranges[i-1].second>ranges[i].first) return Fail(error,"Overlapping ZIP entries.");
    if(pos!=directory.size() || total!=m.unpacked || !paths.count("eboot.bin") ||
       !paths.count("game.bin") || !paths.count("boot/intro.vtm") ||
       !paths.count("sce_sys/param.sfo") || !paths.count("sce_sys/package/head.bin"))
        return Fail(error,"This is not a complete GTA LCS package.");
    return true;
}
}

int Version(const std::string &s) {
    if(s.size()!=5 || s[2]!='.') return -1;
    for(unsigned i:{0U,1U,3U,4U}) if(s[i]<'0'||s[i]>'9') return -1;
    return (s[0]-'0')*1000+(s[1]-'0')*100+(s[3]-'0')*10+s[4]-'0';
}
bool ParseManifest(const std::string &text, Manifest &out, std::string &error) {
    out={};
    if(text.size()>4096 || text.find('\0')!=std::string::npos ||
       text.compare(0,15,"RELCS-UPDATE-1\n")) return Fail(error,"Invalid update information.");
    std::vector<std::string> fields;
    size_t pos=15;
    while(pos<text.size()) {
        size_t end=text.find('\n',pos);
        if(end==std::string::npos) return Fail(error,"Incomplete update information.");
        fields.push_back(text.substr(pos,end-pos)); pos=end+1;
    }
    const char *keys[]={"version=","size=","unpacked_size=","sha256=","url="};
    if(fields.size()!=5) return Fail(error,"Invalid update fields.");
    for(unsigned i=0;i<5;++i) {
        const size_t n=strlen(keys[i]);
        if(fields[i].compare(0,n,keys[i])) return Fail(error,"Invalid update fields.");
        fields[i].erase(0,n);
    }
    out.version=fields[0]; out.sha256=fields[3]; out.url=fields[4];
    if(Version(out.version)<0 || !Number(fields[1],out.size) || !Number(fields[2],out.unpacked) ||
       out.size<1024 || out.size>512*1024*1024U || out.unpacked<1024 || out.unpacked>768*1024*1024U ||
       out.sha256.size()!=64) return Fail(error,"Invalid release version or size.");
    for(char c:out.sha256) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))
        return Fail(error,"Invalid release checksum.");
    const std::string expected=std::string(Repository)+"releases/download/v"+out.version+
        "/reLCS-"+out.version+"-intro-complete.vpk";
    if(out.url!=expected) return Fail(error,"Update is not from the GTA LCS repository.");
    return true;
}
bool LaunchUpdate(const char *text) {
    if(!text) return false;
    // The LiveArea parser returns the psla payload; accept only our exact token.
    return !strcmp(text,"-update") || !strcmp(text,"psla:-update");
}
bool SafePath(const std::string &path) {
    if(path.empty() || path.size()>180 || path.front()=='/' || path.back()=='/') return false;
    for(unsigned char c:path) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                                 (c>='0'&&c<='9')||c=='/'||c=='.'||c=='_'||c=='-')) return false;
    size_t pos=0;
    while(pos<path.size()) {
        size_t end=path.find('/',pos); if(end==std::string::npos) end=path.size();
        std::string part=path.substr(pos,end-pos);
        if(part.empty()||part=="."||part=="..") return false;
        pos=end+1;
    }
    return path=="eboot.bin" || path=="game.bin" || path.compare(0,5,"boot/")==0 ||
        path.compare(0,8,"sce_sys/")==0 || path.compare(0,8,"updater/")==0 ||
        path.compare(0,9,"licenses/")==0;
}
bool ValidateSfo(const std::vector<unsigned char> &data, const std::string &version) {
    if(data.size()<20 || U32(data.data())!=0x46535000 || U32(data.data()+4)!=0x101) return false;
    uint32_t keys=U32(data.data()+8), values=U32(data.data()+12), count=U32(data.data()+16);
    if(count>64 || 20+uint64_t(count)*16>keys || keys>=values || values>=data.size()) return false;
    bool title=false, ver=false, memory=false;
    std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        auto e=data.data()+20+16*i;
        uint64_t key=uint64_t(keys)+U16(e), value=uint64_t(values)+U32(e+12);
        uint32_t len=U32(e+4), capacity=U32(e+8);
        if(key>=values || !len || len>capacity || value+capacity>data.size()) return false;
        auto nameEnd=std::find(data.begin()+key,data.begin()+values,0);
        if(nameEnd==data.begin()+values) return false;
        std::string name(data.begin()+key,nameEnd);
        if(!names.insert(name).second) return false;
        if(name=="TITLE_ID" || name=="APP_VER") {
            if(U16(e+2)!=0x204 || data[value+len-1]!=0) return false;
            std::string s(reinterpret_cast<const char*>(data.data()+value),len-1);
            if(name=="TITLE_ID") title=s=="RELCS0001";
            else ver=s==version;
        } else if(name=="ATTRIBUTE2") memory=U16(e+2)==0x404 && len==4 && U32(data.data()+value)==12;
    }
    return title && ver && memory;
}
std::string SfoVersion(const std::vector<unsigned char> &data) {
    if(data.size()<20 || U32(data.data())!=0x46535000) return "";
    uint32_t keys=U32(data.data()+8), values=U32(data.data()+12), count=U32(data.data()+16);
    if(count>64 || 20+uint64_t(count)*16>keys || keys>=values || values>=data.size()) return "";
    for(uint32_t i=0;i<count;++i) {
        auto e=data.data()+20+16*i;
        uint64_t key=uint64_t(keys)+U16(e), value=uint64_t(values)+U32(e+12);
        if(key+8>values || value+6>data.size()) continue;
        if(!memcmp(data.data()+key,"APP_VER\0",8) && U32(e+4)==6 && data[value+5]==0) {
            std::string version(reinterpret_cast<const char*>(data.data()+value),5);
            if(Version(version)>=0 && ValidateSfo(data,version)) return version;
        }
    }
    return "";
}
bool ReadArchive(const char *path, const Manifest &m, std::vector<Entry> &entries, std::string &error) {
    File file(path,"rb");
    return file.f ? Archive(file.f,m,entries,error) : Fail(error,"Cannot open downloaded package.");
}
bool ExtractArchive(const char *path, const char *dest, const Manifest &m,
                    MakeDirectory mkdir, Progress progress, std::string &error) {
    File file(path,"rb");
    std::vector<Entry> entries;
    if(!file.f || !Archive(file.f,m,entries,error)) return false;
    uint64_t total=0;
    unsigned char input[32768], output[32768];
    for(const auto &entry:entries) {
        std::string target=std::string(dest)+"/"+entry.name;
        for(size_t pos=strlen(dest)+1;(pos=target.find('/',pos))!=std::string::npos;++pos)
            if(!mkdir(target.substr(0,pos).c_str())) return Fail(error,"Cannot create package directory.");
        File out(target.c_str(),"wb");
        if(!out.f || fseek(file.f,entry.offset,SEEK_SET)!=0) return Fail(error,"Cannot write package.");
        z_stream stream={};
        if(entry.method==8 && inflateInit2(&stream,-MAX_WBITS)!=Z_OK) return Fail(error,"Cannot start ZIP decoder.");
        uint32_t left=entry.packed; uint64_t written=0;
        uLong crc=crc32(0,nullptr,0); bool okay=true; bool ended=false;
        while(left && okay) {
            size_t n=std::min<size_t>(left,sizeof(input));
            if(!Read(file.f,input,n)) { okay=false; break; }
            left-=n; stream.next_in=input; stream.avail_in=n;
            do {
                size_t produced=n;
                const unsigned char *pixels=input;
                if(entry.method==8) {
                    stream.next_out=output; stream.avail_out=sizeof(output);
                    int result=inflate(&stream,Z_NO_FLUSH);
                    produced=sizeof(output)-stream.avail_out; pixels=output;
                    if(result==Z_STREAM_END) ended=true;
                    else if(result==Z_BUF_ERROR && !produced && !stream.avail_in) break;
                    else if(result!=Z_OK) { okay=false; break; }
                    if(ended && (left || stream.avail_in)) { okay=false; break; }
                }
                if(written+produced>entry.unpacked || fwrite(pixels,1,produced,out.f)!=produced) { okay=false; break; }
                crc=crc32(crc,pixels,produced); written+=produced; total+=produced;
                if(progress && !progress(total,m.unpacked)) { okay=false; error="Update canceled."; break; }
                if(entry.method==0 || ended) break;
            } while(stream.avail_in || stream.avail_out==0);
        }
        if(entry.method==8) inflateEnd(&stream);
        if(fflush(out.f)!=0 || ferror(out.f)) okay=false;
        if(fclose(out.f)!=0) okay=false;
        out.f=nullptr;
        if(!okay || written!=entry.unpacked || crc!=entry.crc || (entry.method==8&&!ended)) {
            if(error.empty()) error="Package extraction failed: "+entry.name;
            return false;
        }
    }
    File sfo((std::string(dest)+"/sce_sys/param.sfo").c_str(),"rb");
    if(!sfo.f || fseek(sfo.f,0,SEEK_END)!=0) return Fail(error,"Cannot read installed version.");
    long size=ftell(sfo.f);
    if(size<20 || size>65536 || fseek(sfo.f,0,SEEK_SET)!=0) return Fail(error,"Invalid package metadata.");
    std::vector<unsigned char> data(size);
    if(!Read(sfo.f,data.data(),data.size()) || !ValidateSfo(data,m.version))
        return Fail(error,"Package identity or version does not match the release.");
    return true;
}
}
