#ifndef POLYMER_RENDER_CHUNK_RENDERER_H_
#define POLYMER_RENDER_CHUNK_RENDERER_H_

#include <polymer/math.h>

#include <polymer/camera.h>
#include <polymer/render/render.h>

namespace polymer {

struct MemoryArena;

namespace world {

struct World;

} // namespace world

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

struct VulkanTexture;

struct ChunkRenderUBO {
  mat4 mvp;
  Vector4f camera;
  float anim_time;
  float sunlight;
  u32 alpha_discard;
};

struct ChunkVertex {
  Vector3f position;

  u32 texture_id;
  u32 packed_light;

  u16 packed_uv;
  u16 packed_frametime;
};

struct ChunkRenderLayout {
  VkDescriptorSetLayout descriptor_layout;
  VkPipelineLayout pipeline_layout;

  bool Create(VkDevice device);
  void Shutdown(VkDevice device);

  DescriptorSet CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool);
};

#define DISPLAY_PERF_STATS 0
struct RenderStatistics {
  u32 chunk_render_count;

  u64 vertex_counts[render::kRenderLayerCount];

  void Reset() {
    chunk_render_count = 0;

    for (size_t i = 0; i < render::kRenderLayerCount; ++i) {
      vertex_counts[i] = 0;
    }
  }
};

struct ChunkFrameCommandBuffers {
  VkCommandBuffer command_buffers[kRenderLayerCount];
};

struct ChunkRenderer {
  VulkanRenderer* renderer;
  RenderPass* render_pass;

  ChunkRenderLayout layout;
  VkPipeline pipeline;
  VkPipeline alpha_pipeline;
  DescriptorSet descriptor_sets[kRenderLayerCount];

  UniformBuffer opaque_ubo;
  UniformBuffer alpha_ubo;

  VkSampler flora_sampler;
  VkSampler leaf_sampler;

  ChunkFrameCommandBuffers frame_command_buffers[kMaxFramesInFlight];

  VulkanTexture* block_textures;

#if DISPLAY_PERF_STATS
  RenderStatistics stats;
#endif

  void Draw(VkCommandBuffer command_buffer, size_t current_frame, world::World& world, Camera& camera, float anim_time,
            float sunlight);

  void CreateLayoutSet(VulkanRenderer& renderer, VkDevice device) {
    layout.Create(device);
    this->renderer = &renderer;
  }

  void OnSwapchainCreate(MemoryArena& trans_arena, Swapchain& swapchain, VkDescriptorPool descriptor_pool);
  void OnSwapchainDestroy(VkDevice device);

  void Shutdown(VkDevice device) {
    layout.Shutdown(device);
  }

private:
  void CreateSamplers(VkDevice device);
  void CreatePipeline(MemoryArena& arena, VkDevice device, VkExtent2D swap_extent);
  void CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool);
};

} // namespace render
} // namespace polymer

#endif
