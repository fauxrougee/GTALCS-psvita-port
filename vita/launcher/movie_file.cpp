#include "movie_file.h"
#include "movie_color.h"
#include "lz4/lz4.h"
#include <cstdlib>
#include <cstring>
#include <zlib.h>

namespace {
uint32_t LE32(const unsigned char *p) {
	return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
uint64_t LE64(const unsigned char *p) { return LE32(p) | uint64_t(LE32(p+4))<<32; }
}

extern "C" void IntroExpandColorPlanes(const uint8_t *in,uint32_t *rgba,unsigned count)
{
	for(unsigned i=0;i<count;++i) {
		const uint8_t g=in[i+count];
		const uint8_t r=in[i]+g,b=in[i+2*count]+g;
		rgba[i]=0xff000000U | uint32_t(b)<<16 | uint32_t(g)<<8 | r;
	}
}

bool IntroMovieFile::Open(const char *path)
{
	Close(); error="none";
	file=fopen(path,"rb");
	if(!file) return Fail("open movie");
	if(fseek(file,0,SEEK_END)!=0) return Fail("seek end");
	const long length=ftell(file);
	if(length<80 || fseek(file,0,SEEK_SET)!=0) return Fail("movie length");
	unsigned char h[80];
	if(fread(h,1,sizeof(h),file)!=sizeof(h)) return Fail("read header");
	const bool compact=memcmp(h,"VITAMV02",8)==0;
	if((!compact && memcmp(h,"VITAMV01",8)) || LE32(h+8)!=Width || LE32(h+12)!=Height ||
	   LE32(h+16)!=FPS || LE32(h+20)!=1 || LE32(h+28)!=Rate || LE32(h+76)!=unsigned(compact) ||
	   crc32(0,h,72)!=LE32(h+72)) return Fail("header format/checksum");
	frames=LE32(h+24);
	if(frames==0 || frames>MaxFrames || LE32(h+32)!=frames*SamplesPerFrame)
		return Fail("duration");
	const uint32_t maxPacked=LE32(h+36);
	if(!maxPacked || maxPacked>unsigned(LZ4_compressBound(FrameBytes)))
		return Fail("compressed frame limit");
	const uint64_t audioOffset=80+frames*16;
	const uint64_t videoOffset=audioOffset+AudioBytes();
	if(LE64(h+40)!=80 || LE64(h+48)!=audioOffset || LE64(h+56)!=videoOffset ||
	   videoOffset>=uint64_t(length)) return Fail("section offsets");
	index=static_cast<Entry*>(calloc(frames,sizeof(Entry)));
	audio=static_cast<int16_t*>(malloc(AudioBytes()));
	compressed=static_cast<char*>(malloc(maxPacked));
	if(compact) colorPlanes=static_cast<uint8_t*>(malloc(Width*Height*3));
	if(!index || !audio || !compressed || (compact && !colorPlanes)) return Fail("movie memory");
	uLong indexCrc=0;
	uint64_t expectedOffset=videoOffset;
	for(unsigned n=0;n<frames;++n){
		unsigned char entry[16];
		if(fread(entry,1,sizeof(entry),file)!=sizeof(entry)) return Fail("read index");
		indexCrc=crc32(indexCrc,entry,sizeof(entry));
		index[n]={LE64(entry),LE32(entry+8),LE32(entry+12)};
		if(index[n].offset!=expectedOffset || !index[n].bytes || index[n].bytes>maxPacked ||
		   index[n].bytes>uint64_t(length)-expectedOffset) return Fail("frame bounds");
		expectedOffset+=index[n].bytes;
	}
	if(indexCrc!=LE32(h+68) || expectedOffset!=uint64_t(length)) return Fail("index checksum/length");
	if(fread(audio,1,AudioBytes(),file)!=AudioBytes()) return Fail("read audio");
	if(crc32(0,reinterpret_cast<const Bytef*>(audio),AudioBytes())!=LE32(h+64))
		return Fail("audio checksum");
	opened=true; return true;
}

bool IntroMovieFile::Decode(unsigned frame, uint32_t *rgba)
{
	if(!opened || !rgba || frame>=frames) return Fail("frame arguments");
	const Entry &e=index[frame];
	if(ftell(file)!=static_cast<long>(e.offset) && fseek(file,static_cast<long>(e.offset),SEEK_SET)!=0)
		return Fail("seek frame");
	if(fread(compressed,1,e.bytes,file)!=e.bytes) return Fail("read frame");
	if(crc32(0,reinterpret_cast<const Bytef*>(compressed),e.bytes)!=e.crc)
		return Fail("frame checksum");
	const unsigned decodedBytes=colorPlanes ? Width*Height*3 : FrameBytes;
	char *out=colorPlanes ? reinterpret_cast<char*>(colorPlanes) : reinterpret_cast<char*>(rgba);
	if(LZ4_decompress_safe(compressed,out,e.bytes,decodedBytes)!=int(decodedBytes))
		return Fail("LZ4 frame");
	if(colorPlanes) IntroExpandColorPlanes(colorPlanes,rgba,Width*Height);
	return true;
}

void IntroMovieFile::Close()
{
	if(file) fclose(file);
	free(index); free(audio); free(compressed); free(colorPlanes);
	colorPlanes=nullptr;
	file=nullptr; index=nullptr; audio=nullptr; compressed=nullptr; frames=0; opened=false;
}
