#pragma once
#include <cstdint>
#include <cstdio>

// Portable production decoder. The host verification decodes the real release
// asset through this exact class; no Vita or media-player mocks are involved.
class IntroMovieFile {
public:
	static constexpr unsigned Width=640, Height=544, FPS=25, Rate=48000;
	static constexpr unsigned SamplesPerFrame=Rate/FPS, FrameBytes=Width*Height*4;
	static constexpr unsigned MaxFrames=3750;
	IntroMovieFile() = default;
	~IntroMovieFile() { Close(); }
	IntroMovieFile(const IntroMovieFile&)=delete;
	IntroMovieFile& operator=(const IntroMovieFile&)=delete;
	bool Open(const char *path);
	bool Decode(unsigned frame, uint32_t *rgba);
	void Close();
	unsigned Frames() const { return frames; }
	const int16_t *Audio() const { return audio; }
	unsigned AudioBytes() const { return frames*SamplesPerFrame*4; }
	const char *Error() const { return error; }
private:
	struct Entry { uint64_t offset; uint32_t bytes, crc; };
	FILE *file=nullptr;
	Entry *index=nullptr;
	int16_t *audio=nullptr;
	char *compressed=nullptr;
	uint8_t *colorPlanes=nullptr;
	unsigned frames=0;
	bool opened=false;
	const char *error="none";
	bool Fail(const char *message) { error=message; return false; }
};
