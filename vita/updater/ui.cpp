#include "platform.h"
#include "font.h"
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <algorithm>

namespace Update {
namespace {
constexpr unsigned W=960,H=544,Stride=2*1024*1024;
SceUID block=-1;
uint32_t *buffers[2]; unsigned back=0;
void Rect(int x,int y,int w,int h,uint32_t color) {
    for(int row=std::max(y,0);row<std::min(y+h,int(H));++row)
        for(int col=std::max(x,0);col<std::min(x+w,int(W));++col) buffers[back][row*W+col]=color;
}
void Text(int x,int y,const std::string &s,uint32_t color,int scale=2) {
    int left=x;
    for(unsigned char c:s) {
        if(c=='\n') { x=left; y+=22; continue; }
        if(x+8*scale>900) { x=left; y+=22; }
        for(int row=0;row<8;++row) for(int col=0;col<8;++col)
            if(UpdateFont[unsigned(c)*8+row]&(0x80>>col))
                Rect(x+col*scale,y+row*scale,scale,scale,color);
        x+=8*scale;
    }
}
}
bool OpenScreen() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    block=sceKernelAllocMemBlock("update display",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,2*Stride,nullptr);
    if(block<0) return false;
    void *base=nullptr;
    if(sceKernelGetMemBlockBase(block,&base)<0 || !base) return false;
    buffers[0]=static_cast<uint32_t*>(base);
    buffers[1]=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(base)+Stride);
    return true;
}
void Screen(const std::string &message,const std::string &detail,int percent,const std::string &buttons) {
    if(block<0 || !buffers[0]) return;
    Rect(0,0,W,H,0xff19130f);
    Rect(0,0,W,6,0xff7165f1);
    Text(48,46,"GTA LIBERTY CITY STORIES",0xffeee7e4,3);
    Text(48,88,"PS VITA UPDATE",0xffaaa4a1);
    Rect(48,132,864,2,0xff3d3530);
    Text(48,180,message,0xffeee7e4);
    Text(48,234,detail,0xffaaa4a1);
    if(percent>=0) {
        Rect(48,370,864,10,0xff3d3530);
        Rect(48,370,864*std::min(percent,100)/100,10,0xff7165f1);
        Text(48,400,std::to_string(percent)+"%",0xffeee7e4);
    }
    Text(48,492,buttons,0xffeee7e4);
    SceDisplayFrameBuf frame={}; frame.size=sizeof(frame); frame.base=buffers[back];
    frame.pitch=W; frame.width=W; frame.height=H; frame.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart(); back^=1;
}
unsigned Buttons() {
    SceCtrlData pad={};
    sceCtrlPeekBufferPositive(0,&pad,1);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    return pad.buttons;
}
void CloseScreen() {
    if(block<0) return;
    if(sceDisplaySetFrameBuf(nullptr,SCE_DISPLAY_SETBUF_NEXTFRAME)<0) return;
    sceDisplayWaitVblankStartMulti(2);
    for(unsigned sync=0;sync<2;++sync) {
        SceDisplayFrameBuf frame={}; frame.size=sizeof(frame);
        if(sceDisplayGetFrameBuf(&frame,static_cast<SceDisplaySetBufSync>(sync))<0 ||
           frame.base==buffers[0] || frame.base==buffers[1]) return;
    }
    sceKernelFreeMemBlock(block); block=-1;
}
}
