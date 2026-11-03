/* Included by gfx.cpp to reuse the normal renderer without emulating extra
 * scanlines. All PPU/renderer state and native pixel buffers are restored.
 * No ROM IDs, addresses, per-game rules or extra CPU/HDMA execution. */
#include "tall.h"
#include <algorithm>
static unsigned tallSetting=0,tallLayers=0,tallRows=0,tallTop=0;
static bool tallSprites=true,tallBusy=false,tallInvalid=false,tallTopReady=false,tallBottomReady=false;
static int tallVirtualY=0;
static std::vector<uint16> tallOutput,tallMain,tallSub;
static std::vector<uint8> tallZ,tallSubZ;
void S9xTallConfigure(unsigned height,unsigned layers,bool sprites){tallSetting=height;tallLayers=layers;tallSprites=sprites;}
unsigned S9xTallHeight(){return tallRows;}
static bool TallSupported(){
    // Hires, interlace and mosaic keep the unmodified native path.
    if(PPU.BGMode==5||PPU.BGMode==6)return false;
    if(PPU.BGMode==7&&Settings.Mode7Hires)return false;
    if(IPPU.Interlace||IPPU.InterlaceOBJ||IPPU.PseudoHires||IPPU.DoubleWidthPixels||IPPU.DoubleHeightPixels||GFX.DoInterlace)return false;
    for(int i=0;i<4;++i)if(PPU.BGMosaic[i]&&PPU.Mosaic>1)return false;
    return true;
}
static void TallBegin(){
    tallRows=tallSetting==1?416:tallSetting;
    if(tallRows)tallRows=std::max<unsigned>(PPU.ScreenHeight,std::min<unsigned>(2048,(tallRows+7)&~7u));
    tallTop=(tallRows>PPU.ScreenHeight?(tallRows-PPU.ScreenHeight)/2:0);
    tallInvalid=false;tallTopReady=tallBottomReady=false;
    tallOutput.assign(size_t(256)*tallRows,0);
}
static void TallBandOBJ(){
    if(!tallBusy)return;
    int smallW=8,smallH=8,largeW=16,largeH=16;
    switch(PPU.OBJSizeSelect){case 1:largeW=largeH=32;break;case 2:largeW=largeH=64;break;case 3:smallW=smallH=16;largeW=largeH=32;break;case 4:smallW=smallH=16;largeW=largeH=64;break;case 5:smallW=smallH=32;largeW=largeH=64;break;case 6:smallW=16;smallH=32;largeW=32;largeH=64;break;case 7:smallW=16;smallH=32;largeW=largeH=32;break;}
    const int limit=Settings.MaxSpriteTilesPerLine==128?128:32;
    for(int row=0;row<IPPU.CurrentLine;++row){
        auto& line=GFX.OBJLines[row];line.RTOFlags=0;line.Tiles=Settings.MaxSpriteTilesPerLine;
        for(int j=0;j<128;++j)line.OBJ[j].Sprite=-1;
        int count=0,worldY=tallVirtualY+row;
        int first=PPU.FirstSprite;
        if(PPU.OAMPriorityRotation&&(PPU.OAMFlip&PPU.OAMAddr&1))first=(first+(worldY&255))&127;
        if(!tallSprites)continue;
        for(int n=0;n<128;++n){
            int s=(first+n)&127;const auto& obj=PPU.OBJ[s];
            int w=obj.Size?largeW:smallW,h=obj.Size?largeH:smallH,x=obj.HPos;
            GFX.OBJWidths[s]=w;
            if(x<=-w||x>=256)continue;
            int y=obj.VPos&255;
            // OAM Y is only eight bits. Pick one nearest representation of
            // the sprite, rather than repeating all sprites every 256 rows.
            if(y>=(int(PPU.ScreenHeight)+256-h)/2)y-=256;
            int dy=worldY-y;if(dy<0||dy>=h)continue;
            if(count==limit)break;
            GFX.OBJVisibleTiles[s]=(std::min(256,x+w)-std::max(0,x)+7)/8;
            line.Tiles-=GFX.OBJVisibleTiles[s];
            line.OBJ[count].Sprite=s;line.OBJ[count].Line=obj.VFlip?(dy^(w-1)):dy;++count;
        }
    }
}
static void TallRenderBand(int origin,unsigned count,unsigned destination,int edge){
    if(!count)return;
    const SGFX saveGfx=GFX;const SBG saveBg=BG;const SPPU savePpu=PPU;const InternalPPU saveIppu=IPPU;
    SLineData saveLines[240];SLineMatrixData saveMatrix[240];
    memcpy(saveLines,LineData,sizeof(saveLines));memcpy(saveMatrix,LineMatrixData,sizeof(saveMatrix));
    const unsigned oldForced=Settings.BG_Forced;
    const size_t size=GFX.ScreenSize;
    // Native renderer buffers include guard rows because flipped tiles may
    // compute an address preceding row zero. Preserve those guards here.
    tallMain.resize(size+64*GFX.RealPPL);tallSub.resize(size);tallZ.resize(size);tallSubZ.resize(size);
    tallBusy=true;
    for(unsigned pos=0;pos<count;){
        unsigned rows=std::min<unsigned>(128,count-pos);tallVirtualY=origin+pos;TileTallY=tallVirtualY;
        PPU=savePpu;IPPU=saveIppu;GFX=saveGfx;BG=saveBg;
        Settings.BG_Forced=oldForced|(tallLayers==1?1:tallLayers==2?14:0);
        GFX.Screen=tallMain.data()+32*GFX.RealPPL;GFX.SubScreen=tallSub.data();GFX.ZBuffer=tallZ.data();GFX.SubZBuffer=tallSubZ.data();
        std::fill(tallMain.begin(),tallMain.end(),0);std::fill(tallSub.begin(),tallSub.end(),0);std::fill(tallZ.begin(),tallZ.end(),0);std::fill(tallSubZ.begin(),tallSubZ.end(),0);
        IPPU.PreviousLine=0;IPPU.CurrentLine=rows;GFX.StartY=GFX.EndY=0;
        for(unsigned y=0;y<240;++y){LineData[y]=saveLines[edge];for(int bg=0;bg<4;++bg)LineData[y].BG[bg].VOffset=uint16(saveLines[edge].BG[bg].VOffset+((PPU.BGMode==2||PPU.BGMode==4)?0:tallVirtualY));LineMatrixData[y]=saveMatrix[edge];}
        S9xUpdateScreen();
        for(unsigned y=0;y<rows;++y)memcpy(tallOutput.data()+size_t(destination+pos+y)*256,GFX.Screen+size_t(y)*GFX.RealPPL,256*2);
        pos+=rows;
    }
    PPU=savePpu;IPPU=saveIppu;GFX=saveGfx;BG=saveBg;Settings.BG_Forced=oldForced;
    memcpy(LineData,saveLines,sizeof(saveLines));memcpy(LineMatrixData,saveMatrix,sizeof(saveMatrix));
    TileTallY=0;tallBusy=false;
}
static void TallBeforeUpdate(){
    if(tallBusy||!tallSetting||!IPPU.RenderThisFrame||IPPU.CurrentLine<=IPPU.PreviousLine)return;
    if(!TallSupported()){tallInvalid=true;return;}
    if(tallInvalid||tallRows<=PPU.ScreenHeight)return;
    if(!tallTopReady&&IPPU.PreviousLine==0){TallRenderBand(-int(tallTop),tallTop,0,0);tallTopReady=true;}
    if(!tallBottomReady&&IPPU.CurrentLine>=PPU.ScreenHeight){
        TallRenderBand(PPU.ScreenHeight,tallRows-tallTop-PPU.ScreenHeight,tallTop+PPU.ScreenHeight,PPU.ScreenHeight-1);tallBottomReady=true;
    }
}
const uint16 *S9xTallCompose(unsigned width,unsigned height){
    if(!tallSetting||tallInvalid||width!=256||height!=unsigned(PPU.ScreenHeight)||!TallSupported()||!tallTopReady||!tallBottomReady)return nullptr;
    for(unsigned y=0;y<height;++y)memcpy(tallOutput.data()+size_t(y+tallTop)*256,GFX.Screen+size_t(y)*GFX.RealPPL,256*2);
    return tallOutput.data();
}
