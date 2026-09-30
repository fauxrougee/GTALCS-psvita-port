#!/usr/bin/env python3
"""Compare production Vita vertex layouts and particle traversal to a baseline.

Native MSVC/GCC with ASan; no GPU or FPS claim. Particle draw arguments, texture,
blend/depth state, order, RNG consumption and trail mutations must match exactly.
"""
from pathlib import Path
import re
import tempfile
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


VERTEX = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using uint32=uint32_t;using uint64=uint64_t;using int32=int;
struct AttribDesc {uint32 index;int32 type,normalized,size;uint32 stride,offset;};
struct Pointer {int size,type,normalized,stride;uint64 offset;unsigned vbo,generation;};
static uint32 mask, changes, vbo, generation;
static Pointer pointers[32];
static void glEnableVertexAttribArray(unsigned i){mask|=1u<<i;changes++;}
static void glDisableVertexAttribArray(unsigned i){mask&=~(1u<<i);changes++;}
static void glVertexAttribPointer(unsigned i,int size,int type,int n,int stride,void *p){
    pointers[i]={size,type,n,stride,(uint64)p,vbo,generation};
}
using uintptr=uintptr_t;constexpr int GL_UNSIGNED_SHORT=5123;
struct InstanceDataHeader {unsigned primType,totalNumVertex;};
struct InstanceData {unsigned minVert,numIndex,offset;int numVertices;};
static std::vector<uint16_t> indexBuffer,submitted;
static unsigned submittedMode,rangeCalls,normalCalls;
static void flushCache(){}
static void glDrawElements(unsigned mode,unsigned count,unsigned type,void *offset){
    assert(type==GL_UNSIGNED_SHORT);unsigned first=(uintptr)offset/2;
    submitted.assign(indexBuffer.begin()+first,indexBuffer.begin()+first+count);
    submittedMode=mode;normalCalls++;
}
static void rangeDraw(unsigned mode,unsigned start,unsigned end,unsigned count,unsigned type,void *offset){
    rangeCalls++;glDrawElements(mode,count,type,offset);normalCalls--;
    for(auto i:submitted)assert(i>=start&&i<=end);
}
static auto glDrawRangeElements=&rangeDraw;
PRODUCTION
DRAW
int main(){
    std::mt19937 random(622);
    for(unsigned draw=0;draw<30000;draw++){
        AttribDesc layout[16];unsigned n=0;uint32 expected=0;
        // Consecutive draws often share a layout, as world geometry does.
        uint32 wanted=(draw%47)?0x27:random()%65536;
        for(unsigned i=0;i<16;i++)if(wanted&(1u<<i)){
            layout[n++]={i,int(random()%3),int(random()%2),int(random()%4+1),32,4*i};
            expected|=1u<<i;
        }
        vbo=random()%20;generation++; // storage/name reuse must not skip pointers
        setAttribPointers(layout,n);
        assert(mask==expected);
        for(unsigned k=0;k<n;k++){
            auto &a=layout[k];auto &p=pointers[a.index];
            assert(p.size==a.size&&p.type==a.type&&p.normalized==a.normalized&&
                   p.stride==a.stride&&p.offset==a.offset&&p.vbo==vbo&&p.generation==generation);
        }
        disableAttribPointers(layout,n);
        if(draw%1237==0){resetVertexInputState();assert(mask==0);}
    }
    resetVertexInputState();changes=0;
    AttribDesc layout[]={{0,0,0,3,32,0},{2,1,1,4,32,12},{5,0,0,2,32,16}};
    for(int i=0;i<1000;i++){setAttribPointers(layout,3);disableAttribPointers(layout,3);}
    assert(changes==3);
    puts("PASS: 30000 vertex layouts, missing attributes, VBO storage reuse, reset; repeated layout 6000 -> 3 enable/disable calls");
    for(unsigned mesh=0;mesh<4000;mesh++){
        unsigned count=mesh%97,first=random()%65,low=random()%65536;
        indexBuffer.assign(first+count,0);unsigned high=low;
        for(unsigned i=first;i<first+count;i++){
            indexBuffer[i]=low+random()%(65536-low);if(indexBuffer[i]>high)high=indexBuffer[i];
        }
        InstanceDataHeader header{random()%2?4u:5u,65536};
        InstanceData inst{low,count,first*2,count?int(high-low+1):0};
        glDrawRangeElements=mesh%5?&rangeDraw:nullptr;
        drawInst_simple(&header,&inst);
        assert(submittedMode==header.primType&&submitted.size()==count);
        for(unsigned i=0;i<count;i++)assert(submitted[i]==indexBuffer[first+i]);
    }
    assert(rangeCalls>0&&normalCalls>0);
    puts("PASS: 4000 indexed meshes: same indices, primitive and offset, 65535 bound, empty meshes and missing-entry fallback");
}
'''

PARTICLES = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using uint32=uint32_t;using int32=int;using int16=int16_t;using uint8=uint8_t;
constexpr bool TRUE=true,FALSE=false;constexpr auto nil=nullptr;
#define PUSH_RENDERGROUP(x) (void)0
#define POP_RENDERGROUP() (void)0
#define DEGTORAD(x) ((x)*0.017453292519943295f)
static float Asin(float x){return std::asin(x);}
static float Abs(float x){return std::fabs(x);}
static float Max(float a,float b){return a>b?a:b;}
enum {PARTICLE_ENGINE_STEAM,PARTICLE_ENGINE_SMOKE,PARTICLE_ENGINE_SMOKE2,
      PARTICLE_CARFLAME_SMOKE,PARTICLE_CARCOLLISION_DUST,PARTICLE_EXHAUST_FUMES,
      PARTICLE_RUBBER_SMOKE,PARTICLE_BURNINGRUBBER_SMOKE,PARTICLE_WATER_HYDRANT,
      PARTICLE_FLYERS,PARTICLE_BEASTIE,PARTICLE_RAINDROP,PARTICLE_RAINDROP_SMALL,
      PARTICLE_RAINDROP_2D,MAX_PARTICLES=20};
using tParticleType=int;
enum {DRAW_OPAQUE=1,DRAW_DARK=2,DRAWTOP2D=4,SCREEN_TRAIL=8,SPEED_TRAIL=16,VERT_TRAIL=32};
enum {LOOKING_LEFT,LOOKING_RIGHT,LOOKING_FORWARD};
enum {rwRENDERSTATETEXTUREADDRESS,rwRENDERSTATETEXTUREPERSPECTIVE,rwRENDERSTATEFOGENABLE,
      rwRENDERSTATEZWRITEENABLE,rwRENDERSTATEVERTEXALPHAENABLE,rwRENDERSTATESRCBLEND,
      rwRENDERSTATEDESTBLEND,rwRENDERSTATETEXTURERASTER,rwRENDERSTATEZTESTENABLE};
enum {rwTEXTUREADDRESSWRAP=1,rwBLENDSRCALPHA=2,rwBLENDINVSRCALPHA=3,rwBLENDONE=4};
struct CVector {
    float x=0,y=0,z=0;
    CVector operator-(const CVector &v)const{return {x-v.x,y-v.y,z-v.z};}
    float Magnitude()const{return std::sqrt(x*x+y*y+z*z);}
};
struct CVector2D {
    float x,y;CVector2D(float a,float b):x(a),y(b){}
    float Magnitude()const{return std::sqrt(x*x+y*y);}
};
struct RwRGBA {uint8 red,green,blue,alpha;};
struct RwRaster {int id;};
struct CParticle {
    uint8 m_nAlpha=0,m_nCurrentFrame=0;int16 m_nRotation=0,m_nColorIntensity=0;
    float m_fSize=0,m_fZGround=0,m_fExpansionRate=0;
    uint32 m_nTimeWhenWillBeDestroyed=0;
    CVector m_vecPosition,m_vecVelocity;RwRGBA m_Color{};CParticle *m_pNext=nullptr;
    static void Render();
};
struct tParticleSystemData {
    CParticle *m_pParticles=nullptr;RwRaster **m_ppRaster=nullptr;
    int m_Type=0,m_nFinalAnimationFrame=0;uint32 Flags=0;
    CVector m_vecTextureStretch;float m_fTrailLengthMultiplier=0;
};
static struct {tParticleSystemData m_aParticles[MAX_PARTICLES];} mod_ParticleSystemManager;
static struct {int look;int GetLookDirection(){return look;}} TheCamera;
struct CTimer {
    static uint32 GetTimeInMilliseconds(){return 120;}
    static float GetTimeStep(){return .8f;}
};
static uint32 rng;
struct CGeneral {
    static float GetRandomNumberInRange(float a,float b){
        rng=rng*1664525+1013904223;return a+(b-a)*float(rng%65536)/65536;
    }
};
static float CalcScreenZ(float z){return z*.01f;}
static uintptr_t states[9];
static void RwRenderStateSet(int s,void *v){states[s]=(uintptr_t)v;}
struct Event {
    int kind=0;uint64_t args[12]{};uintptr_t texture=0,src=0,dst=0;bool depth=false;
};
static std::vector<Event> pending, output;
static unsigned capacity, batches, switchDepth;
static uint64_t pack(RwRGBA c){return c.red|(c.green<<8)|(c.blue<<16)|(uint32(c.alpha)<<24);}
template<class T>static uint64_t pack(T v){double d=double(v);uint64_t bits;memcpy(&bits,&d,8);return bits;}
struct CSprite {
    static void InitSpriteBuffer2D(){}
    static void FlushSpriteBuffer(){
        if(pending.empty())return;
        batches++;
        for(auto e:pending){
            e.texture=states[rwRENDERSTATETEXTURERASTER];
            e.src=states[rwRENDERSTATESRCBLEND];e.dst=states[rwRENDERSTATEDESTBLEND];
            e.depth=switchDepth?false:bool(states[rwRENDERSTATEZTESTENABLE]);output.push_back(e);
        }
        if(switchDepth)states[rwRENDERSTATEZTESTENABLE]=true;
        pending.clear();
    }
    static bool CalcScreenCoors(CVector in,CVector *out,float *w,float *h,bool){
        if(in.z<=2 || in.z>=600)return false;
        *out={in.x/in.z,in.y/in.z,in.z};*w=100/in.z;*h=150/in.z;return true;
    }
    template<class... T>static void sprite(int kind,bool is2D,T... args){
        switchDepth=is2D;Event e{};e.kind=kind;uint64_t packed[]={pack(args)...};
        static_assert(sizeof...(args)<=12);memcpy(e.args,packed,sizeof(packed));pending.push_back(e);
        if(pending.size()==capacity)FlushSpriteBuffer();
    }
    template<class... T>static void RenderBufferedOneXLUSprite2D(T... a){sprite(0,true,a...);}
    template<class... T>static void RenderBufferedOneXLUSprite2D_Rotate_Dimension(T... a){sprite(1,true,a...);}
    template<class... T>static void RenderBufferedOneXLUSprite(T... a){sprite(2,false,a...);}
    template<class... T>static void RenderBufferedOneXLUSprite_Rotate_Dimension(T... a){sprite(3,false,a...);}
};
REFERENCE
PRODUCTION
static bool sameEvent(const Event &a,const Event &b){
    return a.kind==b.kind&&memcmp(a.args,b.args,sizeof(a.args))==0&&
           a.texture==b.texture&&a.src==b.src&&a.dst==b.dst&&a.depth==b.depth;
}
static void reset(unsigned size){
    assert(pending.empty());output.clear();capacity=size;batches=0;switchDepth=0;
    memset(states,0,sizeof(states));states[rwRENDERSTATEZTESTENABLE]=true;
    states[rwRENDERSTATETEXTURERASTER]=123; rng=901;
}
int main(){
    std::mt19937 random(7123);RwRaster rasters[8];RwRaster *frames[MAX_PARTICLES][5];
    CParticle particles[MAX_PARTICLES][130], initial[MAX_PARTICLES][130], reference[MAX_PARTICLES][130];
    size_t compared=0;unsigned oldBatches=0,newBatches=0;
    for(unsigned scene=0;scene<200;scene++){
        memset(particles,0,sizeof(particles));
        for(int type=0;type<MAX_PARTICLES;type++){
            auto &s=mod_ParticleSystemManager.m_aParticles[type];
            s.Flags=random()%64;s.m_Type=type;s.m_ppRaster=random()%7?frames[type]:nullptr;
            s.m_nFinalAnimationFrame=random()%2?4:0;s.m_vecTextureStretch={.1f,.2f,0};s.m_fTrailLengthMultiplier=.7f;
            for(int f=0;f<5;f++)frames[type][f]=&rasters[random()%8];
            int n=random()%131;s.m_pParticles=n?particles[type]:nullptr;
            for(int i=0;i<n;i++){
                auto &p=particles[type][i];p.m_pNext=i+1<n?&particles[type][i+1]:nullptr;
                p.m_nAlpha=random()%4?uint8(random()%255):0;p.m_nCurrentFrame=random()%5;
                p.m_nRotation=random()%3?int16(random()%360):0;p.m_nColorIntensity=random()%255;
                p.m_fSize=float(random()%100)/100;p.m_nTimeWhenWillBeDestroyed=800;
                p.m_vecPosition={float(int(random()%200)-100),float(random()%200),float(random()%700)};
                p.m_vecVelocity={.31f,.78f,float(int(random()%100)-50)};
                p.m_Color={uint8(random()%255),uint8(random()%255),uint8(random()%255),255};
                p.m_fZGround=random()%2?.9f:0;p.m_fExpansionRate=.8f;
            }
        }
        // Dense compatible smoke: cross-type batching and capacities >64.
        if(scene==0)for(auto &s:mod_ParticleSystemManager.m_aParticles){
            s.Flags=DRAW_OPAQUE;s.m_nFinalAnimationFrame=0;s.m_ppRaster=frames[0];
        }
        TheCamera.look=random()%3;memcpy(initial,particles,sizeof(particles));
        reset(64);renderReference();auto expected=output;auto rngExpected=rng;
        uintptr_t expectedStates[9];memcpy(expectedStates,states,sizeof(states));
        memcpy(reference,particles,sizeof(particles));oldBatches+=batches;
        memcpy(particles,initial,sizeof(particles));
        reset(OPTIMIZED_CAPACITY);CParticle::Render();newBatches+=batches;
        assert(output.size()==expected.size());assert(rng==rngExpected);
        assert(memcmp(states,expectedStates,sizeof(states))==0);
        assert(memcmp(particles,reference,sizeof(particles))==0);
        for(size_t i=0;i<output.size();i++)assert(sameEvent(output[i],expected[i]));
        compared+=output.size();
    }
    assert(newBatches<oldBatches);
    printf("PASS: %zu particle draws across 200 scenes: same arguments/order/raster/blend/depth, RNG and trails; batches %u -> %u\n",compared,oldBatches,newBatches);
}
'''


def main():
    gl = (ROOT/'vendor/librw/src/gl/gl3render.cpp').read_text()
    vertex = gl[gl.index('#if defined(PSP2) && !defined(RW_GL_USE_VAOS)'):
                gl.index('void\nsetupVertexInput(')]
    particle = function((ROOT/'src/renderer/Particle.cpp').read_text(), 'void CParticle::Render()')
    reference = (ROOT/'tools/vita/fixtures/particle-render-reference.inc').read_text()
    sprite = (ROOT/'src/renderer/Sprite.cpp').read_text()
    capacity = re.search(r'#ifdef PSP2\s*//[^\n]*\n#define SPRITEBUFFERSIZE (\d+)', sprite)[1]
    with tempfile.TemporaryDirectory(prefix='relcs-state-') as tmp:
        p = Path(tmp)
        for name, code in [('vertex', VERTEX.replace('PRODUCTION', vertex).replace(
                               'DRAW', function(gl, 'void\ndrawInst_simple('))),
                           ('particles', PARTICLES.replace('REFERENCE', reference).replace(
                               'PRODUCTION', particle).replace('OPTIMIZED_CAPACITY', capacity))]:
            cpp = p/(name+'.cpp'); cpp.write_text(code)
            exe, env = build(p/name, [cpp], defines=['PSP2'])
            print(run(exe, env))


if __name__ == '__main__':
    main()
