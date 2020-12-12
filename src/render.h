#ifndef POLYMER_RENDER_H_
#define POLYMER_RENDER_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define VK_USE_PLATFORM_WIN32_KHR
#include "vk_mem_alloc.h"

#include "buffer.h"
#include "memory.h"
#include "types.h"

namespace polymer {

constexpr size_t kMaxFramesInFlight = 2;

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
  size_t vertex_buffer_size;
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
  VkBuffer vertex_buffer;
  VmaAllocation vertex_allocation;

  VkCommandPool command_pool;
  VkCommandBuffer command_buffers[6];
  VkCommandBuffer oneshot_command_buffer;
  VkSemaphore image_available_semaphores[kMaxFramesInFlight];
  VkSemaphore render_finished_semaphores[kMaxFramesInFlight];
  VkFence frame_fences[kMaxFramesInFlight];
  VkFence image_fences[6];

  size_t current_frame = 0;
  bool render_paused;
  bool invalid_swapchain;

  bool Initialize(HWND hwnd);
  void Render();
  void Cleanup();

  // Uses staging buffer to push data to the gpu and returns the allocation buffers.
  RenderMesh AllocateMesh(u8* data, size_t size, size_t count);
  void FreeMesh(RenderMesh* mesh);

private:
  u32 FindMemoryType(u32 type_filter, VkMemoryPropertyFlags properties);
  void BeginOneShotCommandBuffer();
  void EndOneShotCommandBuffer();
  void CreateVertexBuffer();
  void CleanupSwapchain();
  void RecreateSwapchain();
  void CreateSyncObjects();
  void CreateCommandBuffers();
  void CreateCommandPool();
  void CreateFramebuffers();
  void CreateRenderPass();
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
