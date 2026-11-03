// GPL-3.0-or-later. Generic RGB565 -> RGBA8888 bitmap filtering, no core API.
#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <vector>
#include <mutex>
#include <chrono>
#include <cstring>
#include "filter_engine.h"
static std::mutex filterLock;
static std::vector<uint16_t> previous;
static std::vector<uint32_t> input,output;
static int cachedWidth,cachedHeight,cachedFactor;
static unsigned frames;
extern "C" JNIEXPORT jboolean JNICALL Java_local_sfc_tall_VideoEffects_nativeXbrz(JNIEnv* env,jclass,jobject source,jobject destination,jint width,jint height,jint factor){
    std::lock_guard<std::mutex> guard(filterLock);
    AndroidBitmapInfo a,b;void* src=nullptr;void* dst=nullptr;
    if(AndroidBitmap_getInfo(env,source,&a)||AndroidBitmap_getInfo(env,destination,&b)||a.format!=ANDROID_BITMAP_FORMAT_RGB_565||b.format!=ANDROID_BITMAP_FORMAT_RGBA_8888||width<1||height<1||width>720||height>2048||(factor!=2&&factor!=3)||a.width<(unsigned)width||a.height<(unsigned)height||b.width!=(unsigned)(width*factor)||b.height!=(unsigned)(height*factor))return JNI_FALSE;
    if(AndroidBitmap_lockPixels(env,source,&src))return JNI_FALSE;
    bool ok=true;auto start=std::chrono::steady_clock::now();
    try{
        bool changed=width!=cachedWidth||height!=cachedHeight||factor!=cachedFactor||previous.size()!=size_t(width)*height;
        if(!changed)for(int y=0;y<height&&!changed;++y)changed=std::memcmp(previous.data()+size_t(y)*width,(char*)src+size_t(y)*a.stride,width*2)!=0;
        if(changed){
            tallfilters::upscale((uint16_t*)src,width,height,a.stride/2,factor,input,output);
            previous.resize(size_t(width)*height);for(int y=0;y<height;++y)std::memcpy(previous.data()+size_t(y)*width,(char*)src+size_t(y)*a.stride,width*2);
            cachedWidth=width;cachedHeight=height;cachedFactor=factor;
        }
        if(AndroidBitmap_lockPixels(env,destination,&dst))ok=false;
        else{
            for(int y=0;y<height*factor;++y){uint32_t* row=(uint32_t*)((char*)dst+size_t(y)*b.stride);const uint32_t* line=output.data()+size_t(y)*width*factor;for(int x=0;x<width*factor;++x)row[x]=tallfilters::rgbaBytes(line[x]);}
            AndroidBitmap_unlockPixels(env,destination);
        }
        if(++frames%180==0)__android_log_print(ANDROID_LOG_INFO,"TallFilter","xBRZ%d %dx%d changed=%d ms=%.2f",factor,width,height,changed,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }catch(...){ok=false;}
    AndroidBitmap_unlockPixels(env,source);return ok?JNI_TRUE:JNI_FALSE;
}
