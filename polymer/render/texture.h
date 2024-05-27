#ifndef POLYMER_RENDER_TEXTURE_H_
#define POLYMER_RENDER_TEXTURE_H_

#include <polymer/memory.h>
#include <polymer/render/vulkan.h>

namespace polymer {
namespace render {

struct TextureConfig {
  bool brighten_mipping = false;
  bool anisotropy = false;
  bool enable_mipping = false;
  VkFilter mag_filter = VK_FILTER_NEAREST;
  VkFilter min_filter = VK_FILTER_NEAREST;
  float min_lod = 0.0f;
  float max_lod = 1.0f;
};

struct VulkanTexture {
  VmaAllocation allocation;
  VkImage image;
  VkImageView image_view;
  VkSampler sampler;

  u16 mips;
  u16 depth;

  // Width and height must be the same for texture arrays
  u32 width;
  u32 height;

  u32 channels;
  VkFormat format;

  VulkanTexture* next;
  VulkanTexture* prev;
};

// Stores state for creating a data push command to fill the texture array.
// This allows it to push all of the textures at once for improved performance.
struct TextureArrayPushState {
  enum class Status { Success, ErrorBuffer, Initial };

  VulkanTexture& texture;

  VkBuffer buffer;
  VmaAllocation alloc;
  VmaAllocationInfo alloc_info;
  Status status;

  // Size of one texture with its mips.
  size_t texture_data_size;

  TextureArrayPushState(VulkanTexture& texture)
      : texture(texture), buffer(), alloc(), alloc_info(), status(Status::Initial), texture_data_size(0) {}
};

struct VulkanTextureManager {
  VulkanTexture* textures = nullptr;
  VulkanTexture* last = nullptr;
  VulkanTexture* free = nullptr;

  VulkanTexture* CreateTexture(MemoryArena& arena) {
    VulkanTexture* result = nullptr;

    if (free) {
      result = free;
      free = free->next;
    } else {
      result = memory_arena_push_type(&arena, VulkanTexture);
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

  void ReleaseTexture(VulkanTexture& texture) {
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
    VulkanTexture* current = textures;

    while (current) {
      VulkanTexture* texture = current;
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
