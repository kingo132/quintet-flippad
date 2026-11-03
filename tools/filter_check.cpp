#include "../android/filter_engine.h"
#include "../android/xbrz/xbrz.h"
#include <vector>
#include <iostream>
#include <chrono>
#include <fstream>
#include <stdexcept>
static int count;
static void check(bool yes,const char*msg){if(!yes)throw std::runtime_error(msg);++count;}
int main(int argc,char**argv){
    using namespace tallfilters;
    check(argb565(0xf800)==0xffff0000,"red RGB565");check(argb565(0x07e0)==0xff00ff00,"green RGB565");check(argb565(0x001f)==0xff0000ff,"blue RGB565");
    check(rgbaBytes(0xffff0000)==0xff0000ff,"red RGBA bytes");check(rgbaBytes(0xff0000ff)==0xffff0000,"blue RGBA bytes");
    std::vector<uint32_t> input,out;
    for(int factor:{2,3})for(int w:{1,4,17})for(uint16_t color:{uint16_t(0),uint16_t(0xffff),uint16_t(0xf800),uint16_t(0x001f)}){
        std::vector<uint16_t> src((w+3)*7,0x07e0);for(int y=0;y<7;y++)for(int x=0;x<w;x++)src[y*(w+3)+x]=color;
        upscale(src.data(),w,7,w+3,factor,input,out);check(out.size()==size_t(w)*7*factor*factor,"output dimensions");for(uint32_t p:out)check(p==argb565(color),"uniform color and padded stride");
    }
    std::vector<uint16_t> diagonal(16*16,0);for(int y=0;y<16;y++)for(int x=0;x<16;x++)if(x>=y)diagonal[y*16+x]=0xffff;
    for(int factor:{2,3}){upscale(diagonal.data(),16,16,16,factor,input,out);int blended=0;for(uint32_t p:out){check((p>>24)==255,"opaque alpha");blended+=p!=0xff000000&&p!=0xffffffff;}check(blended>0,"edge blending differs from nearest");}
    // Compare full real-scene sized output with the upstream buffered xBRZ operation.
    for(int factor:{2,3}){
        int w=270,h=416,pw=w+4,ph=h+4;std::vector<uint16_t> src(w*h);
        for(int y=0;y<h;y++)for(int x=0;x<w;x++)src[y*w+x]=uint16_t((x*71+y*157)^((x/7+y/11)&1?0x831f:0));
        upscale(src.data(),w,h,w,factor,input,out);std::vector<uint32_t> serial(size_t(pw)*ph*factor*factor);
        xbrz::scale(factor,input.data(),serial.data(),pw,ph,xbrz::ColorFormat::ARGB);
        for(int y=0;y<h*factor;y++)for(int x=0;x<w*factor;x++)check(out[size_t(y)*w*factor+x]==serial[size_t(y+2*factor)*pw*factor+x+2*factor],"filter equals upstream at every pixel");
    }
    std::cout<<"filter checks passed: "<<count<<"\n";
    if(argc==5){int w=std::stoi(argv[2]),h=std::stoi(argv[3]),factor=std::stoi(argv[4]);std::vector<uint16_t> source(size_t(w)*h);std::ifstream file(argv[1],std::ios::binary);file.read((char*)source.data(),source.size()*2);check(bool(file),"benchmark input");double total=0,worst=0;int n=90;
        for(int i=0;i<n;i++){source[i%source.size()]^=1;auto start=std::chrono::steady_clock::now();upscale(source.data(),w,h,w,factor,input,out);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();total+=ms;if(ms>worst)worst=ms;}
        std::cout<<"xBRZ"<<factor<<" "<<w<<"x"<<h<<" mean_ms="<<total/n<<" max_ms="<<worst<<" frames="<<n<<"\n";
    }
}
