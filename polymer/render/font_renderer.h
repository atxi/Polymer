#ifndef POLYMER_RENDER_FONT_RENDERER_H_
#define POLYMER_RENDER_FONT_RENDERER_H_

#include <polymer/math.h>
#include <polymer/render/render.h>

namespace polymer {

struct MemoryArena;

namespace render {

struct VulkanTexture;

enum FontStyleFlag {
  FontStyle_None = 0,
  FontStyle_DropShadow = (1 << 0),
  FontStyle_Background = (1 << 1),
  FontStyle_Center = (1 << 2),
};
using FontStyleFlags = u32;

struct FontRenderUBO {
  mat4 mvp;
};

struct FontVertex {
  Vector3f position;
  u32 rgba;

  u16 glyph_id;
  u16 uv_xy;
};

struct FontPushBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation buffer_alloc = VK_NULL_HANDLE;
  VmaAllocationInfo buffer_alloc_info;
  size_t vertex_count = 0;

  inline FontVertex* GetMapped() {
    return (FontVertex*)buffer_alloc_info.pMappedData;
  }

  inline void Destroy(VmaAllocator allocator) {
    vmaDestroyBuffer(allocator, buffer, buffer_alloc);
    buffer = nullptr;
  }
};

struct FontPipelineLayout {
  VkDescriptorSetLayout descriptor_layout;
  VkPipelineLayout pipeline_layout;

  bool Create(VkDevice device);
  void Shutdown(VkDevice device);

  DescriptorSet CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool);
};

struct FontRenderer {
  VulkanRenderer* renderer;
  RenderPass* render_pass;

  FontPipelineLayout layout;
  VkPipeline render_pipeline;

  UniformBuffer uniform_buffer;
  DescriptorSet descriptors;

  FontPushBuffer push_buffer;
  VkCommandBuffer command_buffers[kMaxFramesInFlight];

  VulkanTexture* glyph_page_texture;
  u8* glyph_size_table;

  void RenderText(const Vector3f& screen_position, const String& str, FontStyleFlags style = FontStyle_None,
                  const Vector4f& color = Vector4f(1, 1, 1, 1));

  void RenderText(const Vector3f& screen_position, const WString& wstr, FontStyleFlags style = FontStyle_None,
                  const Vector4f& color = Vector4f(1, 1, 1, 1));

  void RenderBackground(const Vector3f& screen_position, const String& str,
                        const Vector4f& color = Vector4f(0.2f, 0.2f, 0.2f, 0.5f));

  void RenderBackground(const Vector3f& screen_position, const Vector2f& size,
                        const Vector4f& color = Vector4f(0.2f, 0.2f, 0.2f, 0.5f));

  int GetTextWidth(const String& str);
  int GetTextWidth(const WString& wstr);

  bool BeginFrame(size_t current_frame);
  void Draw(VkCommandBuffer command_buffer, size_t current_frame);

  void CreateLayoutSet(VulkanRenderer& renderer, VkDevice device);

  void OnSwapchainCreate(MemoryArena& trans_arena, Swapchain& swapchain, VkDescriptorPool descriptor_pool);
  void OnSwapchainDestroy(VkDevice device);

  void Shutdown(VkDevice device) {
    layout.Shutdown(device);
    push_buffer.Destroy(renderer->allocator);
  }

private:
  void CreatePipeline(MemoryArena& arena, VkDevice device, VkExtent2D swap_extent);
  void CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool);
};

} // namespace render
} // namespace polymer

#endif
