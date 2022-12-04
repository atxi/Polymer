#ifndef POLYMER_RENDER_FONT_RENDERER_H_
#define POLYMER_RENDER_FONT_RENDERER_H_

#include "../math.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include "vulkan/vulkan.h"

#include "vk_mem_alloc.h"

namespace polymer {

struct MemoryArena;

namespace render {

struct TextureArray;
struct VulkanRenderer;

struct FontRenderUBO {
  mat4 mvp;
};

struct FontVertex {
  Vector3f position;
  Vector2f uv;
  u32 glyph_id;
};

struct FontRenderPipeline {
  VkDescriptorSetLayout descriptor_layout;
  VkPipelineLayout pipeline_layout;

  bool Create(VkDevice device);
  void Cleanup(VkDevice device);
};

constexpr size_t kFontRenderMaxCharacters = 2048;

struct FontRenderer {
  VulkanRenderer* renderer;
  VmaAllocator allocator;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation buffer_alloc = VK_NULL_HANDLE;
  VmaAllocationInfo buffer_alloc_info;
  size_t vertex_count = 0;

  TextureArray* glyph_page_texture;
  u8* glyph_size_table;

  FontRenderPipeline pipeline;

  VkBuffer uniform_buffers[2];
  VmaAllocation uniform_allocations[2];

  VkRenderPass render_pass;
  VkPipeline render_pipeline;
  VkCommandBuffer command_buffers[2];

  VkDescriptorSet descriptors[2];

  VkSemaphore finished_semaphores[2];

  bool BeginFrame(VkRenderPassBeginInfo render_pass_info, size_t current_frame);
  VkSemaphore SubmitCommands(VkDevice device, VkQueue graphics_queue, size_t current_frame, VkSemaphore wait_semaphore);

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
  void CreatePipeline(MemoryArena& arena, VkDevice device, VkExtent2D swap_extent);
  void CreateCommandBuffers(VulkanRenderer& renderer, VkDevice device, VkCommandPool command_pool);

  void CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool);

  void Destroy(VkDevice device, VkCommandPool command_pool);

  void CreateSyncObjects(VkDevice device);
  void CleanupSwapchain(VkDevice device);

  void RenderText(const Vector3f& screen_position, const String& str);

  void CreateLayoutSet(VulkanRenderer& renderer, VkDevice device);

  void Cleanup(VkDevice device) {
    pipeline.Cleanup(device);
    vmaDestroyBuffer(allocator, buffer, buffer_alloc);
  }
};

} // namespace render
} // namespace polymer

#endif
