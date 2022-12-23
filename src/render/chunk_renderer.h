#ifndef POLYMER_RENDER_CHUNK_RENDERER_H_
#define POLYMER_RENDER_CHUNK_RENDERER_H_

#include "../math.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include "vulkan/vulkan.h"

#include "vk_mem_alloc.h"

namespace polymer {

struct MemoryArena;

namespace render {

enum class RenderLayer {
  Standard,
  Flora,
  Leaves,
  Alpha,

  Count,
};

constexpr size_t kRenderLayerCount = (size_t)RenderLayer::Count;
extern const char* kRenderLayerNames[kRenderLayerCount];

struct TextureArray;
struct VulkanRenderer;

struct ChunkRenderUBO {
  mat4 mvp;
  u32 frame;
};

struct ChunkVertex {
  Vector3f position;

  u32 texture_id;
  u32 tint_index;

  u16 packed_uv;
};

struct BlockRenderer {
  VkRenderPass render_pass;
  VkPipeline pipeline;
  VkCommandBuffer command_buffers[2];

  VkDescriptorSet descriptors[2];

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
};

struct FloraRenderer {
  VkRenderPass render_pass;
  VkPipeline pipeline;

  VkCommandBuffer command_buffers[2];
  VkDescriptorSet descriptors[2];
  VkSampler sampler;

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
};

struct LeafRenderer {
  VkRenderPass render_pass;
  VkPipeline pipeline;

  VkCommandBuffer command_buffers[2];
  VkDescriptorSet descriptors[2];
  VkSampler sampler;

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
};

struct AlphaRenderer {
  VkRenderPass render_pass;
  VkPipeline pipeline;
  VkCommandBuffer command_buffers[2];

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
};

struct ChunkRenderPipeline {
  VkDescriptorSetLayout descriptor_layout;
  VkPipelineLayout pipeline_layout;

  bool Create(VkDevice device);
  void Cleanup(VkDevice device);
};

struct ChunkRenderer {
  VulkanRenderer* renderer;

  ChunkRenderPipeline pipeline;

  VkBuffer uniform_buffers[2];
  VmaAllocation uniform_allocations[2];

  TextureArray* block_textures;

  BlockRenderer block_renderer;
  FloraRenderer flora_renderer;
  LeafRenderer leaf_renderer;
  AlphaRenderer alpha_renderer;

  VkSemaphore block_finished_semaphores[2];

  bool BeginFrame(VkRenderPassBeginInfo render_pass_info, size_t current_frame);
  VkSemaphore SubmitCommands(VkDevice device, VkQueue graphics_queue, size_t current_frame,
                             VkSemaphore image_available_semaphore, VkFence frame_fence);

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
  void CreatePipeline(MemoryArena& arena, VkDevice device, VkExtent2D swap_extent);
  void CreateCommandBuffers(VkDevice device, VkCommandPool command_pool);

  void CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool);

  void Destroy(VkDevice device, VkCommandPool command_pool);

  void CreateSyncObjects(VkDevice device);
  void CleanupSwapchain(VkDevice device);

  void CreateLayoutSet(VulkanRenderer& renderer, VkDevice device) {
    pipeline.Create(device);
    this->renderer = &renderer;
  }

  void Cleanup(VkDevice device) {
    pipeline.Cleanup(device);
  }
};

} // namespace render
} // namespace polymer

#endif
