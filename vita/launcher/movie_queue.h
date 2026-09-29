#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "movie_file.h"

// One decoding producer, one display consumer. A published slot remains owned
// by the consumer until its last pixel is copied; release/acquire also protects
// the non-atomic frame number and pixels. Audio uses its own immutable PCM data.
class IntroFrameQueue {
public:
	static constexpr unsigned Capacity=8;
	bool Allocate() {
		for(auto &s:slots) {
			s.rgba=static_cast<uint32_t*>(malloc(IntroMovieFile::FrameBytes));
			if(!s.rgba) { Free(); return false; }
		}
		return true;
	}
	void Free() {
		for(auto &s:slots) { free(s.rgba); s.rgba=nullptr; s.ready.store(false); }
		write=read=0;
	}
	uint32_t *BeginWrite() {
		auto &s=slots[write%Capacity];
		return s.ready.load(std::memory_order_acquire) ? nullptr : s.rgba;
	}
	void Publish(unsigned frame) {
		auto &s=slots[write%Capacity]; s.frame=frame;
		s.ready.store(true,std::memory_order_release); ++write;
	}
	// Discard only obsolete frames; independent blocks allow recovery from a
	// slow storage read without losing audio synchronisation for the whole film.
	int Present(unsigned target, uint32_t *display, unsigned &dropped) {
		for(;;) {
			auto &s=slots[read%Capacity];
			if(!s.ready.load(std::memory_order_acquire) || s.frame>target) return -1;
			const unsigned frame=s.frame;
			auto &next=slots[(read+1)%Capacity];
			const bool newer=frame<target && next.ready.load(std::memory_order_acquire) && next.frame<=target;
			if(!newer) {
				for(unsigned y=0;y<IntroMovieFile::Height;++y)
					memcpy(display+y*960+160,s.rgba+y*IntroMovieFile::Width,IntroMovieFile::Width*4);
			} else ++dropped;
			s.ready.store(false,std::memory_order_release); ++read;
			if(!newer) return int(frame);
		}
	}
private:
	struct Slot { std::atomic<bool> ready{false}; unsigned frame=0; uint32_t *rgba=nullptr; };
	Slot slots[Capacity];
	unsigned write=0,read=0;
};
