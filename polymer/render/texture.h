#ifndef POLYMER_RENDER_TEXTURE_H_
#define POLYMER_RENDER_TEXTURE_H_

#include <polymer/memory.h>
#include <polymer/render/vulkan.h>

namespace polymer {
namespace render {

struct TextureConfig {
  bool brighten_mipping;

  TextureConfig() : brighten_mipping(true) {}
  TextureConfig(bool brighten_mipping) : brighten_mipping(brighten_mipping) {}
};

struct TextureArray {
  VmaAllocation allocation;
  VkImage image;
  VkImageView image_view;
  VkSampler sampler;

  u16 mips;
  u16 depth;

  // Width and height must be the same
  // TODO: This shouldn't be required, but the mip generator might need updated.
  u32 dimensions;

  u32 channels;
  VkFormat format;

  TextureArray* next;
  TextureArray* prev;
};

// Stores state for creating a data push command to fill the texture array.
// This allows it to push all of the textures at once for improved performance.
struct TextureArrayPushState {
  enum class Status { Success, ErrorBuffer, Initial };

  TextureArray& texture;

  VkBuffer buffer;
  VmaAllocation alloc;
  VmaAllocationInfo alloc_info;
  Status status;

  // Size of one texture with its mips.
  size_t texture_data_size;

  TextureArrayPushState(TextureArray& texture)
      : texture(texture), buffer(), alloc(), alloc_info(), status(Status::Initial), texture_data_size(0) {}
};

struct TextureArrayManager {
  TextureArray* textures = nullptr;
  TextureArray* last = nullptr;
  TextureArray* free = nullptr;

  TextureArray* CreateTexture(MemoryArena& arena) {
    TextureArray* result = nullptr;

    if (free) {
      result = free;
      free = free->next;
    } else {
      result = memory_arena_push_type(&arena, TextureArray);
    }

    result->next = textures;
    result->prev = nullptr;

    if (textures) {
      textures->prev = result;
    }
    textures = result;

    if (last == nullptr) {
      last = result;
    }

    return result;
  }

  void ReleaseTexture(TextureArray& texture) {
    if (texture.prev) {
      texture.prev->next = texture.next;
    }

    if (texture.next) {
      texture.next->prev = texture.prev;
    }

    if (&texture == last) {
      last = last->prev;
    }
  }

  void Clear() {
    TextureArray* current = textures;

    while (current) {
      TextureArray* texture = current;
      current = current->next;

      texture->next = free;
      free = texture;
    }

    last = nullptr;
    textures = nullptr;
  }
};

} // namespace render
} // namespace polymer

#endif
