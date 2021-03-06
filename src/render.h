#ifndef POLYMER_RENDER_H_
#define POLYMER_RENDER_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define VK_USE_PLATFORM_WIN32_KHR
#include "vk_mem_alloc.h"

#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

#include "buffer.h"
#include "math.h"
#include "memory.h"

namespace polymer {

constexpr size_t kMaxFramesInFlight = 2;

struct UniformBufferObject {
  mat4 mvp;
};

struct ChunkVertex {
  Vector3f position;
  Vector2f texcoord;
  u32 texture_id;
  u32 tint_index;
};

struct QueueFamilyIndices {
  u32 graphics;
  bool has_graphics;

  u32 present;
  bool has_present;

  bool IsComplete() {
    return has_graphics && has_present;
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;

  u32 format_count;
  VkSurfaceFormatKHR* formats;

  u32 present_mode_count;
  VkPresentModeKHR* present_modes;
};

struct RenderMesh {
  VkBuffer vertex_buffer;
  VmaAllocation vertex_allocation;
  size_t vertex_count;
};

struct VulkanRenderer {
  MemoryArena* trans_arena;
  HWND hwnd;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkSwapchainKHR swapchain;
  VkQueue graphics_queue;
  VkQueue present_queue;

  u32 swap_image_count;
  VkImage swap_images[6];
  VkImageView swap_image_views[6];
  VkFramebuffer swap_framebuffers[6];
  VkFormat swap_format;
  VkExtent2D swap_extent;
  VkRenderPass render_pass;
  VkPipelineLayout pipeline_layout;
  VkPipeline graphics_pipeline;

  VmaAllocator allocator;

  VkDescriptorPool descriptor_pool;
  VkDescriptorSetLayout descriptor_layout;
  VkDescriptorSet descriptor_sets[kMaxFramesInFlight];
  VkBuffer uniform_buffers[kMaxFramesInFlight];
  VmaAllocation uniform_allocations[kMaxFramesInFlight];

  VkCommandPool command_pool;
  VkCommandBuffer command_buffers[kMaxFramesInFlight];
  VkCommandBuffer oneshot_command_buffer;
  VkSemaphore image_available_semaphores[kMaxFramesInFlight];
  VkSemaphore render_finished_semaphores[kMaxFramesInFlight];
  VkFence frame_fences[kMaxFramesInFlight];
  VkFence image_fences[6];

  VkImage depth_image;
  VmaAllocation depth_allocation;
  VkImageView depth_image_view;

  VmaAllocation texture_allocation;
  VkImage texture_image;
  VkImageView texture_image_view;
  VkSampler texture_sampler;
  u32 texture_mips;

  size_t current_frame = 0;
  u32 current_image = 0;
  bool render_paused;
  bool invalid_swapchain;

  // A list of staging buffers that need to be freed after pushing the oneshot allocation command buffer.
  VkBuffer staging_buffers[2048];
  VmaAllocation staging_allocs[2048];
  size_t staging_buffer_count = 0;

  bool Initialize(HWND hwnd);
  void RecreateSwapchain();
  bool BeginFrame();

  void Render();
  void Cleanup();

  void CreateDescriptorSetLayout();

  // Uses staging buffer to push data to the gpu and returns the allocation buffers.
  RenderMesh AllocateMesh(u8* data, size_t size, size_t count);
  void FreeMesh(RenderMesh* mesh);

  void CreateTexture(size_t width, size_t height, size_t layers);
  void PushTexture(u8* texture, size_t size, size_t index);

  void BeginMeshAllocation();
  void EndMeshAllocation();

private:
  u32 FindMemoryType(u32 type_filter, VkMemoryPropertyFlags properties);

  void GenerateMipmaps(u32 index);
  void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, u32 layer);
  void CreateDepthBuffer();
  void CreateUniformBuffers();
  void BeginOneShotCommandBuffer();
  void EndOneShotCommandBuffer();
  void CleanupSwapchain();
  void CreateSyncObjects();
  void CreateCommandBuffers();
  void CreateCommandPool();
  void CreateFramebuffers();
  void CreateRenderPass();
  void CreateDescriptorPool();
  void CreateDescriptorSets();
  void CreateGraphicsPipeline();
  VkShaderModule CreateShaderModule(SizedString code);
  void CreateImageViews();
  void CreateSwapchain();
  VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
  VkPresentModeKHR ChooseSwapPresentMode(VkPresentModeKHR* present_modes, u32 present_mode_count);
  VkSurfaceFormatKHR ChooseSwapSurfaceFormat(VkSurfaceFormatKHR* formats, u32 format_count);
  SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
  bool CreateWindowSurface(HWND hwnd, VkSurfaceKHR* surface);
  u32 AddUniqueQueue(VkDeviceQueueCreateInfo* infos, u32 count, u32 queue_index);
  void CreateLogicalDevice();
  QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
  bool IsDeviceSuitable(VkPhysicalDevice device);
  bool DeviceHasExtensions(VkPhysicalDevice device);
  bool PickPhysicalDevice();
  void SetupDebugMessenger();
  bool CheckValidationLayerSupport();
  bool CreateInstance();
};

} // namespace polymer

#endif
