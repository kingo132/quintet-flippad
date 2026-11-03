#ifndef TALL_FILTER_ENGINE_H
#define TALL_FILTER_ENGINE_H
#include <cstdint>
#include <vector>
namespace tallfilters {
uint32_t argb565(uint16_t value);
uint32_t rgbaBytes(uint32_t argb);
void upscale(const uint16_t* source,int width,int height,int stride,int factor,std::vector<uint32_t>& input,std::vector<uint32_t>& output);
}
#endif
