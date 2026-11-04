#ifndef TALL_FILTER_ENGINE_H
#define TALL_FILTER_ENGINE_H
#include <cstdint>
#include <vector>
namespace tallfilters {
uint32_t argb565(uint16_t value);
uint32_t rgbaBytes(uint32_t argb);
void upscale(const uint16_t* source,int width,int height,int stride,int factor,std::vector<uint32_t>& input,std::vector<uint32_t>& output);
// All cached pixels are display-only; the game core has no access to this data.
class Palette {
    std::vector<int16_t> indices_;
    std::vector<uint16_t> colors_;
    std::vector<float> distances_;
public:
    Palette();
    void clear();
    int add(uint16_t color);
    const float* distances()const{return distances_.data();}
};
class FrameFilter {
    int width_=0,height_=0,factor_=0;
    std::vector<uint16_t> previous_;
    std::vector<uint32_t> input_,output_;
    std::vector<unsigned char> dirty_;
    Palette palette_;
    bool indexed_=true;unsigned retry_=0;
public:
    bool update(const uint16_t* source,int width,int height,int stride,int factor);
    const std::vector<uint32_t>& pixels()const{return output_;}
    const std::vector<unsigned char>& dirtyRows()const{return dirty_;}
    unsigned scaledRows()const;
    void invalidate()noexcept{width_=height_=factor_=0;}
};
}
#endif
