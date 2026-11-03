// GPL-3.0-or-later: standalone bitmap filter adapter; xBRZ notices in xbrz/.
#include "filter_engine.h"
#include "xbrz/xbrz.h"
#include <stdexcept>
#include <algorithm>
namespace tallfilters {
uint32_t argb565(uint16_t v){unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31;return 0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|((b<<3)|(b>>2));}
uint32_t rgbaBytes(uint32_t p){return(p&0xff00ff00u)|((p&0x00ff0000u)>>16)|((p&0x000000ffu)<<16);}
void upscale(const uint16_t* source,int width,int height,int stride,int factor,std::vector<uint32_t>& input,std::vector<uint32_t>& output){
    if(!source||width<1||height<1||width>720||height>2048||stride<width||(factor!=2&&factor!=3))throw std::invalid_argument("bitmap filter geometry");
    // xBRZ's ARGB path treats out-of-image pixels as transparent. A framebuffer
    // is opaque: replicate two border pixels, then crop the filtered padding.
    const int border=2,pw=width+2*border,ph=height+2*border;
    input.resize(size_t(pw)*ph);
    for(int y=0;y<ph;++y)for(int x=0;x<pw;++x){int sx=std::max(0,std::min(width-1,x-border)),sy=std::max(0,std::min(height-1,y-border));input[size_t(y)*pw+x]=argb565(source[size_t(sy)*stride+sx]);}
    static thread_local std::vector<uint32_t> padded;
    padded.resize(size_t(pw)*ph*factor*factor);
    // Reuse xBRZ's color-distance lookup table on Pixel 9. This avoids
    // repeating floating point color transforms for every pixel of a live map.
    xbrz::scale(factor,input.data(),padded.data(),pw,ph,xbrz::ColorFormat::ARGB);
    output.resize(size_t(width)*height*factor*factor);
    for(int y=0;y<height*factor;++y)std::copy_n(padded.data()+size_t(y+border*factor)*pw*factor+border*factor,width*factor,output.data()+size_t(y)*width*factor);
}
}
