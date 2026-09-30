#!/usr/bin/env python3
"""Compare actual renderer cache code with uncached values at each mock draw.

Uses production registration, per-shader initialization, set/flushUniforms,
shared-light upload, environment calculation, and librw matrix arithmetic.
GL calls and camera/timecycle data are mocked. This is not GPU or FPS testing.
"""
from pathlib import Path
import tempfile
from host_build import build, run

ROOT = Path(__file__).resolve().parents[2]
GL = ROOT/'vendor/librw/src/gl'
shader = (GL/'gl3shader.cpp').read_text()
pipes = (ROOT/'src/extras/custompipes_gl.cpp').read_text()
base = (ROOT/'vendor/librw/src/base.cpp').read_text()


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


PREAMBLE = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <limits>
#include <random>
#include "rwbase.h"
using GLuint=unsigned; using GLint=int; using GLfloat=float;
constexpr int GL_FALSE=0;
#include "rwgl3shader.h"
static unsigned masks[8], selected, uploads, matrixCalculations;
static unsigned char gpu[8][rw::gl3::MAX_UNIFORMS][256];
static void glUseProgram(unsigned p){selected=p;}
static int glGetUniformLocation(unsigned p,const char *n){
    int i=rw::gl3::findUniform(n); assert(i>=0);
    return masks[p]&(1u<<i)?i:-1;
}
static void upload(int loc,int n,const void *data,unsigned size){
    if(loc<0) return;
    assert(loc<rw::gl3::MAX_UNIFORMS && size*n<=256);
    memcpy(gpu[selected][loc],data,n*size); uploads++;
}
static void glUniform1fv(int l,int n,const float *v){upload(l,n,v,4);}
static void glUniform3fv(int l,int n,const float *v){upload(l,n,v,12);}
static void glUniform4fv(int l,int n,const float *v){upload(l,n,v,16);}
static void glUniform4iv(int l,int n,const int *v){upload(l,n,v,16);}
static void glUniformMatrix4fv(int l,int n,int,const float *v){upload(l,n,v,64);}
static void glDeleteProgram(unsigned p){memset(gpu[p],0,sizeof(gpu[p]));}
namespace rw { namespace gl3 {
static bool alphaTest;
static bool getAlphaTest(){return alphaTest;}
}}
#define rwNewT(TYPE,N,...) ((TYPE*)calloc(N,sizeof(TYPE)))
#define rwFree free
'''

MOCK_CAMERA = r'''
namespace rw {
struct Frame { Matrix ltm; Matrix *getLTM(){return &ltm;} };
struct Camera { Frame *frame; Frame *getFrame(){return frame;} };
struct Engine { Camera *currentCamera; };
static Engine *engine;
}
using rw::int32;
constexpr int NUMEXTRADIRECTIONALS=4;
struct CTimeCycle {
    static rw::RGBAf color;
    static float GetAmbientRed(){return color.red;}
    static float GetAmbientGreen(){return color.green;}
    static float GetAmbientBlue(){return color.blue;}
};
rw::RGBAf CTimeCycle::color;
struct Light {rw::RGBAf color;};
static Light ambient, *pAmbient=&ambient;
'''

TEST = r'''
static void worldShaders(){
    using namespace rw::gl3;using namespace CustomPipes;
    Shader ps{}, mobile{}, psOpaque{}, mobileOpaque{};
    ps.program=0;mobile.program=1;psOpaque.program=2;mobileOpaque.program=3;
    leedsWorldShader=&ps;leedsWorldShader_mobile=&mobile;
    for(int missing=0;missing<2;missing++)for(int mode=0;mode<2;mode++)for(int alpha=0;alpha<2;alpha++){
        WorldPipeSwitch=mode?WORLDPIPE_MOBILE:0;alphaTest=alpha!=0;
        leedsWorldShader_noAT=missing?nullptr:&psOpaque;
        leedsWorldShader_mobile_noAT=missing?nullptr:&mobileOpaque;
        useWorldShader();assert(selected==unsigned((mode?1:0)+(!alpha&&!missing?2:0)));
    }
    currentShader=nullptr;
    puts("PASS: world shader selection: both pipelines, alpha transitions and compiler-failure fallback");
}
static rw::gl3::Shader *makeShader(unsigned program,unsigned mask){
    using namespace rw; using namespace rw::gl3;
    masks[program]=mask;
    Shader *sh=rwNewT(Shader,1,0); int i;
    INIT_SHADER
    return sh;
}
static void uniforms(){
    using namespace rw; using namespace rw::gl3;
    int vec=registerUniform("test_vec",UNIFORM_VEC4);
    int ivec=registerUniform("test_ivec",UNIFORM_IVEC4);
    int matrix=registerUniform("test_matrix",UNIFORM_MAT4);
    int direct=registerUniform("test_direct");
    CustomPipes::CustomPipeRegisterGL();
    const unsigned all=(1u<<uniformRegistry.numUniforms)-1;
    Shader *programs[]={makeShader(0,all),makeShader(1,all),
        makeShader(2,all&~(1u<<CustomPipes::u_amb)),makeShader(3,0),
        makeShader(4,1u<<matrix)};
    float v[4]={}, mat[16]={}, cs[4]={}, spec[20]={}; int iv[4]={};
    // First-use zeros must be uploaded, even though the shared serial is 0.
    programs[0]->use(); setUniform(vec,v); flushUniforms();
    assert(uploads>0);
    unsigned baseline=uploads; flushUniforms(); assert(uploads==baseline);
    std::mt19937 random(1701);
    for(unsigned draw=0;draw<15000;draw++){
        Shader *sh=programs[(draw/13)%5]; sh->use();
        if(draw%37==0){
            for(int k=0;k<4;k++){v[k]=float(int(random()%255)-127)/127;iv[k]=random()%255;cs[k]=float(random()%3);}
            for(int k=0;k<16;k++)mat[k]=float(int(random()%31)-15);
            for(int k=0;k<20;k++)spec[k]=float(int(random()%31)-15);
            CTimeCycle::color={v[0],v[1],v[2],1}; ambient.color={v[3],v[2],v[1],v[0]};
        }
        setUniform(vec,v); setUniform(ivec,iv); setUniform(matrix,mat);
        CustomPipes::uploadWorldLights();
        CustomPipes::uploadSharedFloat(CustomPipes::u_shininess,v[0]);
        CustomPipes::uploadSharedVec3(CustomPipes::u_eye,v);
        CustomPipes::uploadSharedVec4Array(CustomPipes::u_specDir,5,spec);
        CustomPipes::uploadSharedVec4(CustomPipes::u_colorscale,cs);
        // Caller-managed uniforms must remain caller-managed.
        glUniform4fv(sh->uniformLocations[direct],1,v);
        flushUniforms();
        if(masks[selected]&(1u<<CustomPipes::u_shininess))
            assert(memcmp(gpu[selected][CustomPipes::u_shininess],v,4)==0);
        if(masks[selected]&(1u<<CustomPipes::u_eye))
            assert(memcmp(gpu[selected][CustomPipes::u_eye],v,12)==0);
        if(masks[selected]&(1u<<CustomPipes::u_specDir))
            assert(memcmp(gpu[selected][CustomPipes::u_specDir],spec,80)==0);
        for(int id=0;id<uniformRegistry.numUniforms;id++){
            if(sh->uniformLocations[id]<0)continue;
            const Uniform &u=uniformRegistry.uniforms[id];
            if(u.type!=UNIFORM_NA){
                unsigned size=uniformTypesize[u.type]*u.num*sizeof(float);
                assert(memcmp(gpu[selected][id],u.data,size)==0);
            }
        }
        assert(!(masks[selected]&(1u<<direct)) || memcmp(gpu[selected][direct],v,16)==0);
        // Verify against the values the original direct calls submitted.
        if(masks[selected]&(1u<<CustomPipes::u_amb))
            assert(memcmp(gpu[selected][CustomPipes::u_amb],&CTimeCycle::color,16)==0);
        if(masks[selected]&(1u<<CustomPipes::u_emiss))
            assert(memcmp(gpu[selected][CustomPipes::u_emiss],&ambient.color,16)==0);
        if(masks[selected]&(1u<<CustomPipes::u_colorscale))
            assert(memcmp(gpu[selected][CustomPipes::u_colorscale],cs,16)==0);
        if(draw==7500){
            // Reusing a program ID must upload all values to the new program.
            currentShader=nullptr; programs[1]->destroy();programs[1]=makeShader(1,all);
        }
    }
    programs[0]->use();
    // New types and the complete specular array must match direct uploads.
        // Check edge bit patterns too: memcmp must distinguish signed zero.
        float edges[]={0.0f,-0.0f,std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity()};
        for(float edge:edges){
            v[0]=edge;spec[19]=edge;
            CustomPipes::uploadSharedFloat(CustomPipes::u_shininess,v[0]);
            CustomPipes::uploadSharedVec3(CustomPipes::u_eye,v);
            CustomPipes::uploadSharedVec4Array(CustomPipes::u_specDir,5,spec);
            flushUniforms();
            assert(memcmp(gpu[selected][CustomPipes::u_shininess],v,4)==0);
            assert(memcmp(gpu[selected][CustomPipes::u_eye],v,12)==0);
            assert(memcmp(gpu[selected][CustomPipes::u_specDir],spec,80)==0);
        }
    // Stable repeated object materials: exactly one upload of each vector.
    programs[0]->use(); flushUniforms();
    CTimeCycle::color={.11f,.22f,.33f,1};ambient.color={.44f,.55f,.66f,1};
    float scale[4]={123,124,125,1}; baseline=uploads;
    for(int i=0;i<1000;i++){
        CustomPipes::uploadWorldLights();
        CustomPipes::uploadSharedFloat(CustomPipes::u_shininess,v[0]);
        CustomPipes::uploadSharedVec3(CustomPipes::u_eye,v);
        CustomPipes::uploadSharedVec4Array(CustomPipes::u_specDir,5,spec);
        CustomPipes::uploadSharedVec4(CustomPipes::u_colorscale,scale);
        flushUniforms();
    }
    assert(uploads-baseline==3);
    printf("PASS: 15000 mock draws, program switches/recreation, absent uniforms, int/matrix values; repeated vectors 3000 -> 3 uploads\n");
    currentShader=nullptr;for(Shader *p:programs)p->destroy();
}
static void environment(){
    using namespace rw;using namespace rw::gl3;
    Frame frames[2]={};Camera camera={&frames[0]};Engine e={&camera};engine=&e;
    std::mt19937 random(509);
    unsigned computations=matrixCalculations;
    for(int i=0;i<12000;i++){
        Frame &f=frames[(i/7)%2];camera.frame=&f;
        if(i%29==0){f.ltm.at.x=float(int(random()%1025)-512);f.ltm.at.y=float(int(random()%1025)-512);}
        // Pitch, position and matrix flags must not affect this reflection.
        f.ltm.at.z=float(random()%500); f.ltm.pos={float(random()%500),float(i),-100};
        f.ltm.flags=random();f.ltm.right={float(i),99,32};f.ltm.up={43,57,float(i)};
        Frame *input=i%3?nullptr:&f;
        CustomPipes::uploadEnvMatrix(input);
        RawMatrix actual=*(RawMatrix*)uniformRegistry.uniforms[CustomPipes::u_texMatrix].data;
        CustomPipes::referenceEnvMatrix(input);
        assert(memcmp(&actual,uniformRegistry.uniforms[CustomPipes::u_texMatrix].data,sizeof(actual))==0);
    }
    assert(matrixCalculations-computations<18000); // includes 12000 reference calculations
    // Signed zero and a reused frame with a changed heading must invalidate.
    const float edge[]={0.0f,-0.0f,1.0f,-1.0f,std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()};
    for(float x:edge)for(float y:edge){
        frames[0].ltm.at={x,y,77};
        CustomPipes::uploadEnvMatrix(&frames[0]);
        RawMatrix actual=*(RawMatrix*)uniformRegistry.uniforms[CustomPipes::u_texMatrix].data;
        CustomPipes::referenceEnvMatrix(&frames[0]);
        assert(memcmp(&actual,uniformRegistry.uniforms[CustomPipes::u_texMatrix].data,sizeof(actual))==0);
    }
    frames[0].ltm.at={.3f,.4f,.5f};computations=matrixCalculations;
    for(int i=0;i<1000;i++)CustomPipes::uploadEnvMatrix(&frames[0]);
    assert(matrixCalculations-computations==1);
    puts("PASS: 12000 reflection comparisons, camera changes, zero/NaN/Inf; repeated matrices 1000 -> 1 calculation");
}
int main(){worldShaders();uniforms();environment();}
'''


def main():
    registry = shader[shader.index('UniformRegistry uniformRegistry;'):shader.index('static void\nprintShaderSource')]
    math = 'namespace rw {\n'
    for signature in ['void\nRawMatrix::mult(', 'Matrix*\nMatrix::invert(', 'void\nMatrix::invertOrthonormal(']:
        f = function(base, signature)
        if 'RawMatrix::mult(' in signature:
            f = f.replace('{', '{ ++matrixCalculations;', 1)
        math += f+'\n'
    math += 'Matrix *Matrix::invertGeneral(Matrix *,const Matrix *){assert(false);return nullptr;}\n}\n'
    init = shader[shader.index('// query uniform locations'):shader.index('// set samplers')]
    declarations = pipes[pipes.index('static int32 u_viewVec;'):pipes.index('#define U(i)')]
    env_constant = pipes[pipes.index('static rw::RawMatrix normal2texcoord_flipU'):pipes.index('static void\nuploadEnvMatrix')]
    env_func = function(pipes, 'static void\nuploadEnvMatrix(')
    custom = 'namespace CustomPipes {\n'+declarations+env_constant
    custom += 'enum {WORLDPIPE_MOBILE=1};static int WorldPipeSwitch;\n'
    custom += pipes[pipes.index('rw::gl3::Shader *leedsWorldShader;'):pipes.index('static void\nworldRenderCB(')]
    for signature in ['static void\nuploadSharedVec4(', 'static void\nuploadSharedFloat(', 'static void\nuploadSharedVec3(', 'static void\nuploadSharedVec4Array(', 'static int32\nregisterSharedUniform(', 'void\nuploadWorldLights(', 'void\nCustomPipeRegisterGL(']:
        custom += function(pipes, signature)+'\n'
    custom += env_func+'\n#undef PSP2\n'+env_func.replace('uploadEnvMatrix(', 'referenceEnvMatrix(', 1)+'\n#define PSP2\n}\n'
    code = PREAMBLE+'namespace rw { namespace gl3 {\n'+registry
    for signature in ['void\nShader::use(', 'void\nShader::destroy(']:
        code += function(shader, signature)+'\n'
    code += '}}\n'+math+MOCK_CAMERA+custom+TEST.replace('INIT_SHADER',init)
    # All four colorscale writers and both light writers must use the helper.
    assert pipes.count('uploadSharedVec4(u_colorscale, colorscale);') == 2
    assert pipes.count('CustomPipes::uploadSharedVec4(CustomPipes::u_colorscale, colorscale);') == 2
    with tempfile.TemporaryDirectory(prefix='relcs-render-') as tmp:
        p=Path(tmp);cpp=p/'render.cpp';cpp.write_text(code)
        exe, env = build(p/'render',[cpp],[GL,GL.parent],['RW_OPENGL','PSP2'])
        print(run(exe,env))


if __name__=='__main__': main()
