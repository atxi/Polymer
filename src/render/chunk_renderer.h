#ifndef POLYMER_RENDER_CHUNK_RENDERER_H_
#define POLYMER_RENDER_CHUNK_RENDERER_H_

#include "../math.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include "vulkan/vulkan.h"

namespace polymer {

enum class RenderLayer {
  Standard,
  Flora,
  Alpha, // Water

  Count,
};

constexpr size_t kRenderLayerCount = (size_t)RenderLayer::Count;

namespace render {

struct UniformBufferObject {
  mat4 mvp;
  u32 frame;
};

struct ChunkVertex {
  Vector3f position;
  Vector2f texcoord;
  u32 texture_id;
  u32 tint_index;
};

struct BlockRenderer {
  VkRenderPass render_pass;
  VkPipeline pipeline;
  VkCommandBuffer command_buffers[2];

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

struct AlphaRenderer {
  VkRenderPass render_pass;
  VkPipeline pipeline;
  VkCommandBuffer command_buffers[2];

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
};

struct ChunkRenderer {
  BlockRenderer block_renderer;
  FloraRenderer flora_renderer;
  AlphaRenderer alpha_renderer;

  VkSemaphore block_finished_semaphores[2];

  bool BeginFrame(VkRenderPassBeginInfo render_pass_info, size_t current_frame, VkPipelineLayout layout,
                  VkDescriptorSet descriptor);
  void SubmitCommands(VkDevice device, VkQueue graphics_queue, size_t current_frame,
                      VkSemaphore image_available_semaphore, VkSemaphore render_finished_semaphore,
                      VkFence frame_fence);

  void CreateRenderPass(VkDevice device, VkFormat swap_format);
  void CreatePipeline(VkDevice device, VkShaderModule vertex_shader, VkShaderModule frag_shader, VkExtent2D swap_extent,
                      VkPipelineLayout pipeline_layout);
  void CreateCommandBuffers(VkDevice device, VkCommandPool command_pool);

  void CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool, VkDescriptorSetLayout* layouts,
                         VkImageView texture_image_view, VkBuffer* uniform_buffers);

  void Destroy(VkDevice device, VkCommandPool command_pool);

  void CreateSyncObjects(VkDevice device);
  void CleanupSwapchain(VkDevice device);
};

} // namespace render
} // namespace polymer

#endif
