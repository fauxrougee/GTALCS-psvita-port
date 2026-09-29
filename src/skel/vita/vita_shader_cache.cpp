#ifdef PSP2
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <zlib.h>
#include "vita_shader_cache.h"
#include "vita_cache_build.h"

namespace {
constexpr unsigned MaxBinary=2*1024*1024,MaxSource=256*1024;
constexpr const char *Directory="ux0:data/reLCS/shader-cache-v1";
const char Identity[]="reLCS-program-v1|ES2|pos0-normal1-color2-weights3-indices4-tex5-6|" VITA_GL_CACHE_BUILD;
bool directoryReady=false;

void Append32(std::string &s,uint32_t v) { for(unsigned i=0;i<4;++i) s+=char(v>>(i*8)); }
uint32_t Read32(const unsigned char *p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
std::string SourceKey(const char **vs,const char **fs)
{
	std::string key(Identity,sizeof(Identity));
	for(const char **part : {vs,fs}) {
		unsigned count=0; while(part[count]) ++count;
		Append32(key,count);
		for(unsigned i=0;i<count;++i) { const size_t n=strlen(part[i]); Append32(key,n); key.append(part[i],n); }
	}
	return key;
}
std::string FileName(const std::string &key)
{
	uint64_t hash=14695981039346656037ULL;
	for(unsigned char c:key) { hash^=c; hash*=1099511628211ULL; }
	char path[160]; snprintf(path,sizeof(path),"%s/%016llx.bin",Directory,static_cast<unsigned long long>(hash));
	return path;
}
bool MakeDirectory()
{
	if(directoryReady) return true;
	sceIoMkdir("ux0:data/reLCS",0777);
	sceIoMkdir(Directory,0777);
	SceIoStat st={}; directoryReady=sceIoGetstat(Directory,&st)>=0;
	return directoryReady;
}
}

bool VitaLoadShaderProgram(const char **vertex,const char **fragment,unsigned int *program)
{
	const std::string key=SourceKey(vertex,fragment);
	if(key.size()>MaxSource) return false;
	const std::string path=FileName(key);
	FILE *f=fopen(path.c_str(),"rb"); if(!f) return false;
	unsigned char h[24];
	bool valid=fread(h,1,sizeof(h),f)==sizeof(h) && !memcmp(h,"LCSGP001",8);
	unsigned sourceSize=valid?Read32(h+8):0,binarySize=valid?Read32(h+12):0;
	valid=valid && sourceSize==key.size() && binarySize>=16 && binarySize<=MaxBinary;
	std::vector<unsigned char> savedKey(valid?sourceSize:0),binary(valid?binarySize:0);
	if(valid) valid=fread(savedKey.data(),1,sourceSize,f)==sourceSize &&
		fread(binary.data(),1,binarySize,f)==binarySize && fgetc(f)==EOF && !ferror(f) &&
		!memcmp(savedKey.data(),key.data(),sourceSize) &&
		crc32(0,savedKey.data(),sourceSize)==Read32(h+16) && crc32(0,binary.data(),binarySize)==Read32(h+20);
	fclose(f);
	if(!valid) { printf("[VITA] Shader cache rejected; recompiling source\n"); return false; }
	const SceUInt64 began=sceKernelGetProcessTimeWide();
	GLuint p=glCreateProgram();
	if(!p) return false;
	glProgramBinary(p,0,binary.data(),binary.size());
	GLint success=0; glGetProgramiv(p,GL_LINK_STATUS,&success);
	if(!success) { glDeleteProgram(p); printf("[VITA] Shader cache load failed; recompiling source\n"); return false; }
	*program=p;
	printf("[VITA] Shader cache hit: %u bytes, %.1f ms\n",binarySize,(sceKernelGetProcessTimeWide()-began)/1000.0);
	return true;
}

void VitaSaveShaderProgram(const char **vertex,const char **fragment,unsigned int program)
{
	if(!MakeDirectory()) return;
	const std::string key=SourceKey(vertex,fragment);
	if(key.size()>MaxSource) return;
	GLint capacity=0; glGetProgramiv(program,GL_PROGRAM_BINARY_LENGTH,&capacity);
	if(capacity<16 || capacity>int(MaxBinary)) return;
	std::vector<unsigned char> binary(capacity);
	GLsizei size=0; GLenum format=0;
	glGetProgramBinary(program,capacity,&size,&format,binary.data());
	if(size<16 || size>capacity) return;
	std::string h("LCSGP001",8);
	Append32(h,key.size()); Append32(h,size);
	Append32(h,crc32(0,reinterpret_cast<const Bytef*>(key.data()),key.size()));
	Append32(h,crc32(0,binary.data(),size));
	const std::string path=FileName(key),temp=path+".tmp";
	FILE *f=fopen(temp.c_str(),"wb"); if(!f) return;
	bool ok=fwrite(h.data(),1,h.size(),f)==h.size() && fwrite(key.data(),1,key.size(),f)==key.size() &&
		fwrite(binary.data(),1,size,f)==static_cast<size_t>(size) && fflush(f)==0;
	if(fclose(f)!=0) ok=false;
	if(ok) { remove(path.c_str()); ok=rename(temp.c_str(),path.c_str())==0; }
	if(!ok) remove(temp.c_str());
	printf("[VITA] Shader cache %s: %d bytes\n",ok?"saved":"write failed",size);
}
#endif
