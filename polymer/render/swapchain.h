#ifndef POLYMER_RENDER_H_
#define POLYMER_RENDER_H_

#include <polymer/render/render_config.h>
#include <polymer/render/vulkan.h>
#include <polymer/types.h>

namespace polymer {

struct MemoryArena;

namespace render {

constexpr size_t kMaxSwapImages = 6;

using SwapchainCallback = void (*)(struct Swapchain& swapchain, void* user_data);

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;

  u32 format_count;
  VkSurfaceFormatKHR* formats;

  u32 present_mode_count;
  VkPresentModeKHR* present_modes;
};

struct FramebufferSet {
  VkFramebuffer framebuffers[kMaxSwapImages];
  size_t count;
};

struct MultisampleState {
  VkImage color_image;
  VkImageView color_image_view;
  VmaAllocation color_image_allocation;

  VkSampleCountFlagBits max_samples;
  VkSampleCountFlagBits samples;
};

struct Swapchain {
  RenderConfig* render_cfg = nullptr;

  VmaAllocator allocator;
  VkSwapchainKHR swapchain;
  VkDevice device;

  VkFormat format;
  VkSampler sampler;
  VkExtent2D extent;

  MultisampleState multisample;

  VkImage depth_image;
  VkImageView depth_image_view;
  VmaAllocation depth_allocation;

  u32 image_count;
  VkImage images[kMaxSwapImages];
  VkImageView image_views[kMaxSwapImages];
  VkFence image_fences[kMaxSwapImages];

  bool supports_linear_mipmap;

  VkPresentModeKHR present_mode;
  VkSurfaceFormatKHR surface_format;
  SwapChainSupportDetails swapchain_support;

  void InitializeFormat(MemoryArena& trans_arena, VkPhysicalDevice physical_device, VkDevice device,
                        VkSurfaceKHR surface);

  void Create(MemoryArena& trans_arena, VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface,
              VkExtent2D extent, struct QueueFamilyIndices& indices);
  void Cleanup();

  // Call this to trigger the create callbacks.
  void OnCreate();

  void RegisterCreateCallback(void* user_data, SwapchainCallback callback);
  void RegisterCleanupCallback(void* user_data, SwapchainCallback callback);

  FramebufferSet CreateFramebuffers(VkRenderPass render_pass);

  static SwapChainSupportDetails QuerySwapChainSupport(MemoryArena& trans_arena, VkPhysicalDevice device,
                                                       VkSurfaceKHR surface);

private:
  void CreateViewBuffers();

  VkPresentModeKHR ChooseSwapPresentMode(VkPresentModeKHR* present_modes, u32 present_mode_count);
  VkSurfaceFormatKHR ChooseSwapSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceFormatKHR* formats,
                                             u32 format_count);

  struct CallbackRegistration {
    void* user_data;
    SwapchainCallback callback;
  };

  CallbackRegistration create_callbacks[16];
  size_t create_callback_size = 0;

  CallbackRegistration cleanup_callbacks[16];
  size_t cleanup_callback_size = 0;
};

} // namespace render
} // namespace polymer

#endif
