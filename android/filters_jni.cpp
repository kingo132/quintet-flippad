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
static tallfilters::FrameFilter filter;
static unsigned frames;
static bool uploadPending;
extern "C" JNIEXPORT jboolean JNICALL Java_local_sfc_tall_VideoEffects_nativeXbrz(JNIEnv* env,jclass,jobject source,jobject destination,jint width,jint height,jint factor,jboolean fresh){
    std::lock_guard<std::mutex> guard(filterLock);
    AndroidBitmapInfo a,b;void* src=nullptr;void* dst=nullptr;
    if(AndroidBitmap_getInfo(env,source,&a)||AndroidBitmap_getInfo(env,destination,&b)||a.format!=ANDROID_BITMAP_FORMAT_RGB_565||b.format!=ANDROID_BITMAP_FORMAT_RGBA_8888||width<1||height<1||width>720||height>2048||(factor!=2&&factor!=3)||a.width<(unsigned)width||a.height<(unsigned)height||b.width!=(unsigned)(width*factor)||b.height!=(unsigned)(height*factor))return JNI_FALSE;
    if(AndroidBitmap_lockPixels(env,source,&src))return JNI_FALSE;
    bool ok=true;auto start=std::chrono::steady_clock::now();
    try{
        bool fullUpload=fresh||uploadPending;
        bool changed=filter.update((uint16_t*)src,width,height,a.stride/2,factor);
        if(changed||fullUpload){
            uploadPending=true;
            if(AndroidBitmap_lockPixels(env,destination,&dst))ok=false;
            else{
                const auto& output=filter.pixels();const auto& dirty=filter.dirtyRows();
                for(int y=0;y<height*factor;++y)if(fullUpload||dirty[y/factor]){
                    uint32_t* row=(uint32_t*)((char*)dst+size_t(y)*b.stride);const uint32_t* line=output.data()+size_t(y)*width*factor;
                    for(int x=0;x<width*factor;++x)row[x]=tallfilters::rgbaBytes(line[x]);
                }
                AndroidBitmap_unlockPixels(env,destination);uploadPending=false;
            }
        }
        if(++frames%180==0)__android_log_print(ANDROID_LOG_INFO,"TallFilter","xBRZ%d %dx%d changed=%d rows=%u ms=%.2f",factor,width,height,changed,filter.scaledRows(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }catch(...){filter.invalidate();uploadPending=true;ok=false;}
    AndroidBitmap_unlockPixels(env,source);return ok?JNI_TRUE:JNI_FALSE;
}
