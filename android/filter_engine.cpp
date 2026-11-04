// GPL-3.0-or-later: standalone bitmap filter adapter; xBRZ notices in xbrz/.
#include "filter_engine.h"
#include "xbrz/xbrz.h"
#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <cstring>
#ifdef __ANDROID__
#include <sched.h>
#include <cstdio>
#endif
namespace tallfilters {
uint32_t argb565(uint16_t v){unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31;return 0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|((b<<3)|(b>>2));}
uint32_t rgbaBytes(uint32_t p){return(p&0xff00ff00u)|((p&0x00ff0000u)>>16)|((p&0x000000ffu)<<16);}
namespace {
void validate(const uint16_t* src,int w,int h,int stride,int factor){
    if(!src||w<1||h<1||w>720||h>2048||stride<w||(factor!=2&&factor!=3))throw std::invalid_argument("bitmap filter geometry");
}
// Avoid dispatching a frame's critical path to efficiency-only CPU cores.
// Android exposes scheduler capacity; respect the caller's permitted CPUs and
// fall back to normal scheduling if capacity/affinity controls are unavailable.
class PerformanceCores {
#ifdef __ANDROID__
    cpu_set_t fast{};bool enabled=false;
public:
    PerformanceCores(){
        cpu_set_t allowed;CPU_ZERO(&allowed);CPU_ZERO(&fast);
        if(sched_getaffinity(0,sizeof(allowed),&allowed))return;
        unsigned capacity[CPU_SETSIZE]={},maximum=0;
        for(int cpu=0;cpu<CPU_SETSIZE;cpu++)if(CPU_ISSET(cpu,&allowed)){
            char path[128];std::snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu%d/cpu_capacity",cpu);
            if(FILE* f=std::fopen(path,"r")){if(std::fscanf(f,"%u",&capacity[cpu])==1)maximum=std::max(maximum,capacity[cpu]);std::fclose(f);}
        }
        if(!maximum)return;
        for(int cpu=0;cpu<CPU_SETSIZE;cpu++)if(capacity[cpu]>maximum/2)CPU_SET(cpu,&fast);
        enabled=CPU_COUNT(&fast)>0;
    }
    unsigned count()const{return enabled?CPU_COUNT(&fast):std::max(1u,std::thread::hardware_concurrency());}
    void apply()const{if(enabled)(void)sched_setaffinity(0,sizeof(fast),&fast);}
    class Scope {
        cpu_set_t previous{};bool restore=false;
    public:
        Scope(const PerformanceCores& cores){restore=cores.enabled&&sched_getaffinity(0,sizeof(previous),&previous)==0;if(restore)cores.apply();}
        ~Scope(){if(restore)(void)sched_setaffinity(0,sizeof(previous),&previous);}
    };
#else
public:
    unsigned count()const{return std::max(1u,std::thread::hardware_concurrency());}
    void apply()const{}
    class Scope{public:Scope(const PerformanceCores&){};};
#endif
};
struct Slice{int first,last;};
// xBRZ explicitly supports concurrent, non-overlapping source-row slices.
// Keep workers alive: creating threads for every 60-Hz frame costs more than
// scaling small animation regions. The caller also executes queued slices.
class SlicePool {
    PerformanceCores cores;
    std::mutex callMutex,mutex;
    std::condition_variable start,finished;
    std::vector<std::thread> workers;
    bool stop=false;
    unsigned generation=0,pending=0;
    std::atomic<unsigned> next{0};
    const uint32_t* src=nullptr;uint32_t* dst=nullptr;
    int width=0,height=0,factor=0;
    const float* palette=nullptr;
    const std::vector<Slice>* slices=nullptr;
    void execute(){
        for(unsigned i;(i=next.fetch_add(1,std::memory_order_relaxed))<slices->size();){
            const Slice s=(*slices)[i];
            xbrz::ScalerCfg cfg;cfg.paletteDistances=palette;
            xbrz::scale(factor,src,dst,width,height,palette?xbrz::ColorFormat::PALETTE:xbrz::ColorFormat::RGB,cfg,s.first,s.last);
            // RGB blending leaves the high byte unspecified. Bit-exact opaque
            // ARGB output is needed by both the Android bitmap and our oracle.
            for(size_t p=size_t(s.first)*factor*width*factor,end=size_t(s.last)*factor*width*factor;p<end;++p)dst[p]=palette&&(dst[p]&0x80000000u)?argb565(uint16_t(dst[p])):dst[p]|0xff000000u;
        }
    }
public:
    SlicePool(){
        unsigned count=std::min(4u,cores.count());
        try{for(unsigned i=1;i<count;i++)workers.emplace_back([this]{
            cores.apply();unsigned seen=0;
            for(;;){
                {std::unique_lock<std::mutex> lock(mutex);start.wait(lock,[&]{return stop||generation!=seen;});if(stop)return;seen=generation;}
                execute();
                {std::lock_guard<std::mutex> lock(mutex);if(--pending==0)finished.notify_one();}
            }
        });}catch(...){shutdown();throw;}
    }
    void shutdown(){
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}start.notify_all();
        for(auto& worker:workers)if(worker.joinable())worker.join();
    }
    ~SlicePool(){shutdown();}
    void scale(const uint32_t* input,uint32_t* output,int w,int h,int f,const std::vector<Slice>& jobs,const float* distances){
        std::lock_guard<std::mutex> call(callMutex);
        PerformanceCores::Scope fast(cores);
        // Warm the shared lookup table on the calling thread. The remaining
        // RGB scale operation performs no allocations on worker threads.
        (void)xbrz::equalColorTest(0xffff0000,0xff00ff00,xbrz::ColorFormat::RGB,1,30);
        palette=distances;src=input;dst=output;width=w;height=h;factor=f;slices=&jobs;next=0;
        if(jobs.size()<2||size_t(w)*h<4096){execute();return;}
        {std::lock_guard<std::mutex> lock(mutex);pending=workers.size();++generation;}
        start.notify_all();execute();
        std::unique_lock<std::mutex> lock(mutex);finished.wait(lock,[&]{return pending==0;});
    }
};
void scaleRows(const std::vector<uint32_t>& input,std::vector<uint32_t>& output,int w,int h,int factor,const std::vector<unsigned char>& dirty,const float* palette=nullptr){
    std::vector<Slice> slices;
    for(int y=0;y<h;y+=32){int end=std::min(h,y+32);if(std::any_of(dirty.begin()+y,dirty.begin()+end,[](unsigned char v){return v!=0;}))slices.push_back({y,end});}
    if(slices.empty())return;
    static SlicePool pool;pool.scale(input.data(),output.data(),w,h,factor,slices,palette);
}
}
Palette::Palette():indices_(65536,-1),distances_(1024*1024){}
void Palette::clear(){std::fill(indices_.begin(),indices_.end(),-1);colors_.clear();}
int Palette::add(uint16_t color){
    int index=indices_[color];if(index>=0)return index;
    index=colors_.size();if(index==1024)return -1;
    for(int i=0;i<index;i++){
        float distance=xbrz::rgbDistance(argb565(color),argb565(colors_[i]));
        distances_[index*1024+i]=distances_[i*1024+index]=distance;
    }
    distances_[index*1024+index]=0;colors_.push_back(color);indices_[color]=index;return index;
}
void upscale(const uint16_t* source,int width,int height,int stride,int factor,std::vector<uint32_t>& input,std::vector<uint32_t>& output){
    validate(source,width,height,stride,factor);
    input.resize(size_t(width)*height);output.resize(size_t(width)*height*factor*factor);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)input[size_t(y)*width+x]=argb565(source[size_t(y)*stride+x]);
    scaleRows(input,output,width,height,factor,std::vector<unsigned char>(height,1));
}
bool FrameFilter::update(const uint16_t* source,int width,int height,int stride,int factor){
    validate(source,width,height,stride,factor);
    bool fresh=width_!=width||height_!=height||factor_!=factor;
    if(fresh){width_=width;height_=height;factor_=factor;previous_.resize(size_t(width)*height);input_.resize(size_t(width)*height);output_.resize(size_t(width)*height*factor*factor);palette_.clear();indexed_=true;retry_=0;}
    if(!indexed_&&++retry_==120){palette_.clear();indexed_=true;retry_=0;fresh=true;}
    dirty_.assign(height,0);bool changed=false,overflow=false;
    for(int y=0;y<height;y++){
        const uint16_t* row=source+size_t(y)*stride;uint16_t* old=previous_.data()+size_t(y)*width;
        if(!fresh&&std::memcmp(row,old,width*2)==0)continue;
        changed=true;std::memcpy(old,row,width*2);
        for(int x=0;x<width;x++){
            int id=indexed_?palette_.add(row[x]):-1;
            if(indexed_&&id<0)overflow=true;
            input_[size_t(y)*width+x]=indexed_&&id>=0?0x80000000u|(unsigned(id)<<16)|row[x]:argb565(row[x]);
        }
        for(int r=std::max(0,y-2);r<std::min(height,y+3);r++)dirty_[r]=1;
    }
    if(overflow){
        // Old scenes can consume the palette IDs. Rebuild for the current
        // frame; unusually color-rich frames safely use the generic RGB path.
        palette_.clear();overflow=false;
        for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(palette_.add(source[size_t(y)*stride+x])<0)overflow=true;
        indexed_=!overflow;changed=true;std::fill(dirty_.begin(),dirty_.end(),1);
        for(int y=0;y<height;y++)for(int x=0;x<width;x++){
            uint16_t c=source[size_t(y)*stride+x];int id=indexed_?palette_.add(c):-1;
            input_[size_t(y)*width+x]=indexed_?0x80000000u|(unsigned(id)<<16)|c:argb565(c);
        }
    }
    if(changed){
        for(int y=0;y<height;y+=32){int end=std::min(height,y+32);if(std::any_of(dirty_.begin()+y,dirty_.begin()+end,[](unsigned char v){return v!=0;}))std::fill(dirty_.begin()+y,dirty_.begin()+end,1);}
        scaleRows(input_,output_,width,height,factor,dirty_,indexed_?palette_.distances():nullptr);
    }
    return changed;
}
unsigned FrameFilter::scaledRows()const{return std::count(dirty_.begin(),dirty_.end(),1);}
}
