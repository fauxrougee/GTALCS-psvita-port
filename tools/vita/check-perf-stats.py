#!/usr/bin/env python3
"""Host test of the Vita measurement statistics (src/skel/vita/vita_perf_stats.cpp).

Builds the actual production file with a test driver under ASan/UBSan: frame
interval summaries and percentiles, one-second FPS windows, capacity overflow,
the deadline frame pacer (with scheduler oversleep and late frames), vblank
hold histograms and flip/swap pairing. It does not run any Vita code.
Usage: python tools/vita/check-perf-stats.py (native Windows or Linux)
"""
from pathlib import Path
import subprocess
import tempfile
from host_build import build, run
ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'src/skel/vita'

TEST=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include "vita_perf_stats.h"
static uint32_t scratch[VitaSampleSeries::CAPACITY];
static VitaSampleSeries series, latencies;
static bool near(double a,double b,double eps=1e-9){ return std::fabs(a-b)<=eps; }

static void summaries(){
    VitaSampleSummary s;
    series.Reset(); series.Summarise(&s,scratch);
    assert(s.count==0 && s.meanMs==0 && s.fpsWindows==0 && s.fpsMin==-1.0f && s.fpsMax==-1.0f);
    // 1..100 ms in shuffled order: nearest-rank percentiles, exact thresholds
    std::mt19937 rng(7); uint32_t v[100]; for(int i=0;i<100;i++) v[i]=(i+1)*1000;
    std::shuffle(v,v+100,rng);
    for(uint32_t x:v) series.Add(x);
    series.Summarise(&s,scratch);
    assert(s.count==100 && near(s.meanMs,50.5) && near(s.medianMs,50) && near(s.p95Ms,95) && near(s.p99Ms,99));
    assert(near(s.minMs,1) && near(s.maxMs,100) && s.over33==67 && s.over50==50);
    // 33.333 ms is not above a 30 fps frame; 33.334 is
    series.Reset(); series.Add(33333); series.Add(33334); series.Add(50000); series.Add(50001);
    series.Summarise(&s,scratch); assert(s.over33==3 && s.over50==1);
    // single sample: every percentile is that sample
    series.Reset(); series.Add(16667); series.Summarise(&s,scratch);
    assert(near(s.medianMs,16.667) && near(s.p99Ms,16.667) && s.fpsWindows==0);
}

static void fpsWindows(){
    VitaSampleSummary s;
    series.Reset();
    for(int i=0;i<60;i++) series.Add(16667);	// closes at 1.00002 s: 60 frames
    for(int i=0;i<30;i++) series.Add(33334);	// 30 frames
    for(int i=0;i<10;i++) series.Add(50000);	// incomplete window: ignored
    series.Summarise(&s,scratch);
    assert(s.fpsWindows==2);
    assert(std::fabs(s.fpsMax-60.0f)<0.01f && std::fabs(s.fpsMin-30.0f)<0.01f);
}

static void overflow(){
    VitaSampleSummary s;
    series.Reset();
    const unsigned n=VitaSampleSeries::CAPACITY+952;
    for(unsigned i=0;i<n;i++) series.Add(i<VitaSampleSeries::CAPACITY ? 10000 : 90000);
    series.Summarise(&s,scratch);
    assert(s.count==n && series.Dropped()==952);
    assert(near(s.maxMs,90) && s.over50==952);		// streaming stats see everything
    assert(near(s.p99Ms,10));				// percentiles: stored samples only
    assert(near(s.meanMs,(VitaSampleSeries::CAPACITY*10.0+952*90.0)/n,1e-6));
}

static void pacer(){
    // 45 fps: 22222 us cadence with 0-1.5 ms oversleep and random work.
    VitaFramePacer p; p.SetPeriod(22222);
    std::mt19937 rng(3); std::uniform_int_distribution<int> work(4000,21000), over(0,1500);
    uint64_t now=1000000, first=0, last=0; unsigned frames=0;
    while(frames<5000){
        uint64_t left=p.Remaining(now);
        if(left==0){
            if(!first) first=now;
            assert(!last || now-last>=21000);	// never early beyond the poll slack
            last=now; frames++;
            now+=work(rng);
        }else if(left>2000) now+=left-1000+over(rng);	// sleep, may oversleep
        else now+=100;					// poll
    }
    double period=(double)(last-first)/(frames-1);
    assert(std::fabs(period-22222)<30);			// no drift over 5000 frames
    // A late frame (> one period) restarts the schedule instead of bursting
    p.SetPeriod(33333); now=0; assert(p.Remaining(now)==0);
    now+=100000; assert(p.Remaining(now)==0);		// late: runs now
    assert(p.Remaining(now+1)==33332);			// next one a full period later
    // Frames slower than the period never wait, and never run back to back
    p.SetPeriod(16667); now=5000000;
    for(int i=0;i<600;i++){ assert(p.Remaining(now)==0); now+=17000; }
}

static void holds(){
    VitaHoldHistogram h; h.Reset();
    h.Add(1); h.Add(2); h.Add(2); h.Add(5); h.Add(0); h.Add(-3);
    assert(h.held[0]==1 && h.held[1]==2 && h.held[2]==0 && h.held[3]==1 && h.Total()==4 && h.vblanks==10);
    assert(near(h.DisplayFPS(),24.0));
    h.Reset(); assert(h.DisplayFPS()==0.0);
    for(int i=0;i<30;i++) h.Add(2);
    assert(near(h.DisplayFPS(),30.0));
}

static void flips(){
    VitaFlipMatcher m=VitaFlipMatcher(); VitaHoldHistogram h; h.Reset(); latencies.Reset();
    // A flip before any submit is ignored
    assert(!m.Flip(0,10,0,&h,&latencies) && m.Unmatched()==1);
    // Idle 1: loading screen + game frame; the loading swap is not gameplay
    uint32_t a=m.Submit(1000,VitaFlipMatcher::PENDING), b=m.Submit(2000,VitaFlipMatcher::PENDING);
    m.SetKind(a,VitaFlipMatcher::OTHER); m.SetKind(b,VitaFlipMatcher::GAME);
    // Idle 2 and 3: game frames
    uint32_t c=m.Submit(35000,VitaFlipMatcher::GAME), d=m.Submit(68000,VitaFlipMatcher::GAME);
    (void)c; (void)d;
    // Display flips of the same four swaps (flip index restarts at 1: one unmatched before)
    assert(!m.Flip(1,100,9000,&h,&latencies));		// loading image
    assert(m.Flip(2,101,20000,&h,&latencies));		// hold of loading image not counted
    assert(m.Flip(3,103,52000,&h,&latencies));		// previous game image held 2
    assert(m.Flip(4,106,85000,&h,&latencies));		// held 3 (missed flip)
    assert(h.Total()==2 && h.held[1]==1 && h.held[2]==1);
    VitaSampleSummary s; latencies.Summarise(&s,scratch);
    assert(s.count==3 && near(s.minMs,17) && near(s.maxMs,18));
    // An unmatched callback cannot be counted as another displayed game image.
    unsigned known=latencies.Count();
    assert(!m.Flip(9,107,90000,&h,&latencies) && m.Unmatched()==2 && latencies.Count()==known);
    uint32_t e=m.Submit(100000,VitaFlipMatcher::GAME); (void)e;
    assert(m.Flip(10,109,110000,&h,&latencies) && m.Unmatched()==2);
    // Submits older than the ring give no latency and no gameplay kind
    for(int i=0;i<VitaFlipMatcher::RING+5;i++) m.Submit(200000+i,VitaFlipMatcher::GAME);
    unsigned before=latencies.Count(), held=h.Total();
    assert(!m.Flip(11,110,300000,&h,&latencies) && latencies.Count()==before && h.Total()==held+1);
}

int main(){ summaries(); fpsWindows(); overflow(); pacer(); holds(); flips(); puts("perf stats OK"); }
'''

def main():
    with tempfile.TemporaryDirectory() as tmp:
        test=Path(tmp)/'test.cpp'; test.write_text(TEST)
        exe=Path(tmp)/'test'
        exe, env = build(exe, [test, SRC/'vita_perf_stats.cpp'], [SRC], ['VITA_PERF_HOST_TEST'])
        print(run(exe, env))
    print('PASS: Vita measurement statistics (native host, sanitizers)')

if __name__=='__main__': main()
