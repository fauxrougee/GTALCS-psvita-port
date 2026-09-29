// Compiled with the production movie_file.cpp and LZ4, on host and ARM32.
#include "movie_file.h"
#include "movie_queue.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

int main(int argc,char **argv)
{
#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	assert(argc>=3);
	IntroMovieFile movie;
	const bool opened=movie.Open(argv[2]);
	if(!strcmp(argv[1],"reject-open")) {
		assert(!opened); fprintf(stderr,"Rejected: %s\n",movie.Error()); return 0;
	}
	if(!opened) { fprintf(stderr,"Open failed: %s\n",movie.Error()); return 1; }
	std::vector<uint32_t> storage(IntroMovieFile::Width*IntroMovieFile::Height+32,0x1234abcd);
	uint32_t *pixels=storage.data()+16;
	if(!strcmp(argv[1],"reject-frame")) {
		assert(!movie.Decode(unsigned(atoi(argv[3])),pixels));
		fprintf(stderr,"Rejected: %s\n",movie.Error()); return 0;
	}
	const bool dump=!strcmp(argv[1],"dump");
	if(dump) assert(fwrite(movie.Audio(),1,movie.AudioBytes(),stdout)==movie.AudioBytes());
	auto begin=std::chrono::steady_clock::now();
	for(unsigned n=0;n<movie.Frames();++n) {
		if(!movie.Decode(n,pixels)) { fprintf(stderr,"Frame %u: %s\n",n,movie.Error()); return 1; }
		for(unsigned i=0;i<16;++i) {
			assert(storage[i]==0x1234abcd);
			assert(storage[storage.size()-1-i]==0x1234abcd);
		}
		if(dump) assert(fwrite(pixels,1,IntroMovieFile::FrameBytes,stdout)==IntroMovieFile::FrameBytes);
	}
	for(unsigned n : {movie.Frames()-1,0U,1000U,18U,277U}) {
		if(n>=movie.Frames()) continue;
		assert(movie.Decode(n,pixels));
		if(dump) assert(fwrite(pixels,1,IntroMovieFile::FrameBytes,stdout)==IntroMovieFile::FrameBytes);
	}
	assert(!movie.Decode(movie.Frames(),pixels));
	assert(!movie.Decode(0,nullptr));
	const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
	fprintf(stderr,"Decoded %u real frames, guarded buffers, sequential + seeks in %.3f seconds\n",movie.Frames(),seconds);
	movie.Close(); assert(!movie.Decode(0,pixels)); movie.Close();
	// Exercise actual SPSC queue on two real threads, including backpressure,
	// late frames, wraparound and preservation of the display's black borders.
	IntroFrameQueue queue; assert(queue.Allocate());
	std::atomic<bool> done(false);
	std::thread producer([&]{
		for(unsigned n=0;n<500;++n) {
			uint32_t *p;
			while(!(p=queue.BeginWrite())) std::this_thread::yield();
			std::fill(p,p+IntroMovieFile::FrameBytes/4,0xff000000|n);
			queue.Publish(n);
		}
		done.store(true);
	});
	std::vector<uint32_t> display(960*544,0xff000000);
	unsigned dropped=0,consumed=0;
	int previous=-1;
	while(previous!=499) {
		int n=queue.Present(499,display.data(),dropped);
		if(n<0) { std::this_thread::yield(); continue; }
		assert(n>previous); previous=n; ++consumed;
		for(unsigned y=0;y<544;++y) for(unsigned x=0;x<960;++x)
			assert(display[y*960+x]==((x<160 || x>=800) ? 0xff000000U : (0xff000000U|unsigned(n))));
	}
	producer.join(); assert(done && consumed+dropped==500); queue.Free();
	fprintf(stderr,"PASS concurrent queue: %u shown, %u obsolete frames discarded, no torn pixels\n",consumed,dropped);
}
