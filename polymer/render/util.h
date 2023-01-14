#ifndef POLYMER_RENDER_UTIL_H_
#define POLYMER_RENDER_UTIL_H_

#include <polymer/render/vulkan.h>

#include <polymer/types.h>

namespace polymer {

struct MemoryArena;

namespace render {

struct Mipmap {
  unsigned char* data;
  size_t dimension;

  Mipmap(unsigned char* data, size_t dimension) : data(data), dimension(dimension) {}

  int Sample(size_t x, size_t y, size_t color_offset) {
    return data[(y * dimension + x) * 4 + color_offset];
  }

  u32 SampleFull(size_t x, size_t y) {
    return *(u32*)&data[(y * dimension + x) * 4];
  }
};

// Performs basic pixel averaging filter for generating mipmap.
void BoxFilterMipmap(u8* previous, u8* data, size_t data_size, size_t dim, bool brighten_mipping);

VkShaderModule CreateShaderModule(VkDevice device, String code);
String ReadEntireFile(const char* filename, MemoryArena* arena);

} // namespace render
} // namespace polymer

#endif
