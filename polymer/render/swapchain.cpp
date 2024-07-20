#include <polymer/render/swapchain.h>

#include <polymer/memory.h>
#include <polymer/render/render.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

namespace polymer {
namespace render {

void Swapchain::InitializeFormat(MemoryArena& trans_arena, VkPhysicalDevice physical_device, VkDevice device,
                                 VkSurfaceKHR surface) {
  this->device = device;

  swapchain_support = QuerySwapChainSupport(trans_arena, physical_device, surface);

  if (swapchain_support.format_count == 0) {
    fprintf(stderr, "Failed to initialize swapchain. No formats supported.\n");
    fflush(stderr);
    exit(1);
  }

  surface_format = ChooseSwapSurfaceFormat(physical_device, swapchain_support.formats, swapchain_support.format_count);
  present_mode = ChooseSwapPresentMode(swapchain_support.present_modes, swapchain_support.present_mode_count);

  VkFormatProperties format_properties;
  vkGetPhysicalDeviceFormatProperties(physical_device, surface_format.format, &format_properties);

  this->supports_linear_mipmap =
      (format_properties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

  if (!this->supports_linear_mipmap) {
    printf("Chose surface format without support for linear mipmap filtering.\n");
    fflush(stdout);
  }
}

void Swapchain::Create(MemoryArena& trans_arena, VkPhysicalDevice physical_device, VkDevice device,
                       VkSurfaceKHR surface, VkExtent2D extent, QueueFamilyIndices& indices) {
  InitializeFormat(trans_arena, physical_device, device, surface);

  if (swapchain_support.capabilities.currentExtent.width != UINT32_MAX) {
    extent = swapchain_support.capabilities.currentExtent;
  }

  if (extent.width == 0 || extent.height == 0) {
    swapchain = VK_NULL_HANDLE;
    image_count = 0;
    return;
  }

  u32 image_count = swapchain_support.capabilities.minImageCount + 1;

  if (swapchain_support.capabilities.maxImageCount > 0 && image_count > swapchain_support.capabilities.maxImageCount) {
    image_count = swapchain_support.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  uint32_t queue_indices[] = {indices.graphics, indices.present};

  if (indices.graphics != indices.present) {
    create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create_info.queueFamilyIndexCount = 2;
    create_info.pQueueFamilyIndices = queue_indices;
  } else {
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;
  }

  create_info.preTransform = swapchain_support.capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create swap chain.\n");
  }

  vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
  assert(image_count < polymer_array_count(images));
  vkGetSwapchainImagesKHR(device, swapchain, &image_count, images);

  format = create_info.imageFormat;
  this->extent = create_info.imageExtent;

  VkSamplerCreateInfo sampler_info = {};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_info.anisotropyEnable = VK_FALSE;
  sampler_info.maxAnisotropy = 0;
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  sampler_info.unnormalizedCoordinates = VK_FALSE;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;

  if (vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create swap sampler.\n");
  }

  for (u32 i = 0; i < image_count; ++i) {
    VkImageViewCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = images[i];
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &create_info, nullptr, &image_views[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create swapchain image view.\n");
    }
  }

  CreateViewBuffers();

  this->image_count = image_count;
}

void Swapchain::OnCreate() {
  for (size_t i = 0; i < create_callback_size; ++i) {
    CallbackRegistration* reg = this->create_callbacks + i;

    reg->callback(*this, reg->user_data);
  }
}

FramebufferSet Swapchain::CreateFramebuffers(VkRenderPass render_pass) {
  FramebufferSet set = {};

  for (u32 i = 0; i < image_count; i++) {
    VkImageView attachments[] = {multisample.color_image_view, depth_image_view, image_views[i]};
    u32 attachment_count = polymer_array_count(attachments);

    if (multisample.samples & VK_SAMPLE_COUNT_1_BIT) {
      attachments[0] = image_views[i];
      attachment_count--;
    }

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = attachment_count;
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = extent.width;
    framebuffer_info.height = extent.height;
    framebuffer_info.layers = 1;

    if (vkCreateFramebuffer(device, &framebuffer_info, nullptr, set.framebuffers + i) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create framebuffer.\n");
    }
  }

  set.count = image_count;

  return set;
}

void Swapchain::CreateViewBuffers() {
  VkImageCreateInfo image_create_info = {};

  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.extent.width = extent.width;
  image_create_info.extent.height = extent.height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  image_create_info.samples = multisample.samples;
  image_create_info.format = this->format;
  image_create_info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  multisample.color_image = nullptr;

  if (multisample.samples != VK_SAMPLE_COUNT_1_BIT) {
    if (vkCreateImage(device, &image_create_info, nullptr, &multisample.color_image) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create color buffer image.\n");
    }
  }

  image_create_info.format = VK_FORMAT_D32_SFLOAT;
  image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  if (vkCreateImage(device, &image_create_info, nullptr, &depth_image) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create depth buffer image.\n");
  }

  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.image = multisample.color_image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = this->format;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_create_info.flags = 0;

  if (multisample.samples != VK_SAMPLE_COUNT_1_BIT) {
    if (vmaAllocateMemoryForImage(allocator, multisample.color_image, &alloc_create_info,
                                  &multisample.color_image_allocation, nullptr) != VK_SUCCESS) {
      fprintf(stderr, "Failed to allocate memory for color buffer.\n");
    }

    vmaBindImageMemory(allocator, multisample.color_image_allocation, multisample.color_image);

    if (vkCreateImageView(device, &view_create_info, nullptr, &multisample.color_image_view) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create color image view.\n");
    }
  }

  view_create_info.image = depth_image;
  view_create_info.format = VK_FORMAT_D32_SFLOAT;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

  if (vmaAllocateMemoryForImage(allocator, depth_image, &alloc_create_info, &depth_allocation, nullptr) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate memory for depth buffer.\n");
  }

  vmaBindImageMemory(allocator, depth_allocation, depth_image);

  if (vkCreateImageView(device, &view_create_info, nullptr, &depth_image_view) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create depth image view.\n");
  }

  printf("Swapchain multisample count: %d\n", multisample.samples);
}

void Swapchain::Cleanup() {
  if (swapchain == VK_NULL_HANDLE || image_count == 0) return;

  vkDestroySampler(device, sampler, nullptr);

  vmaFreeMemory(allocator, depth_allocation);
  vkDestroyImageView(device, depth_image_view, nullptr);
  vkDestroyImage(device, depth_image, nullptr);

  if (multisample.color_image) {
    vmaFreeMemory(allocator, multisample.color_image_allocation);
    vkDestroyImageView(device, multisample.color_image_view, nullptr);
    vkDestroyImage(device, multisample.color_image, nullptr);
    multisample.color_image = nullptr;
  }

  for (size_t i = 0; i < cleanup_callback_size; ++i) {
    CallbackRegistration* reg = this->cleanup_callbacks + i;

    reg->callback(*this, reg->user_data);
  }

  for (u32 i = 0; i < image_count; i++) {
    vkDestroyImageView(device, image_views[i], nullptr);
  }

  vkDestroySwapchainKHR(device, swapchain, nullptr);
}

VkPresentModeKHR Swapchain::ChooseSwapPresentMode(VkPresentModeKHR* present_modes, u32 present_mode_count) {
  VkPresentModeKHR desired_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  // Fall back to VK_PRESENT_MODE_FIFO_KHR since it's required to exist.
  // We change this to immediate if that's found but desired isn't.
  VkPresentModeKHR fallback_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (render_cfg) {
    desired_mode = render_cfg->desired_present_mode;
  }

  for (u32 i = 0; i < present_mode_count; ++i) {
    VkPresentModeKHR mode = present_modes[i];

    if (mode == desired_mode) {
      return mode;
    }

    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      fallback_mode = mode;
    }
  }

  return fallback_mode;
}

VkSurfaceFormatKHR Swapchain::ChooseSwapSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceFormatKHR* formats,
                                                      u32 format_count) {
  size_t best_index = 0;

  for (u32 i = 0; i < format_count; ++i) {
    VkSurfaceFormatKHR* format = formats + i;

    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical_device, format->format, &properties);

    if (format->format == VK_FORMAT_B8G8R8A8_UNORM || format->format == VK_FORMAT_R8G8B8A8_UNORM) {
      if (properties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
        best_index = i;
        break;
      }

      best_index = i;
    }
  }

  return formats[best_index];
}

void Swapchain::RegisterCreateCallback(void* user_data, SwapchainCallback callback) {
  if (create_callback_size < polymer_array_count(create_callbacks)) {
    CallbackRegistration* registration = create_callbacks + create_callback_size++;

    registration->user_data = user_data;
    registration->callback = callback;
  }
}

void Swapchain::RegisterCleanupCallback(void* user_data, SwapchainCallback callback) {
  if (cleanup_callback_size < polymer_array_count(cleanup_callbacks)) {
    CallbackRegistration* registration = cleanup_callbacks + cleanup_callback_size++;

    registration->user_data = user_data;
    registration->callback = callback;
  }
}

SwapChainSupportDetails Swapchain::QuerySwapChainSupport(MemoryArena& trans_arena, VkPhysicalDevice device,
                                                         VkSurfaceKHR surface) {
  SwapChainSupportDetails details = {};

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count, nullptr);

  if (details.format_count != 0) {
    details.formats = memory_arena_push_type_count(&trans_arena, VkSurfaceFormatKHR, details.format_count);

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count, details.formats);
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_mode_count, nullptr);

  if (details.present_mode_count != 0) {
    details.present_modes = memory_arena_push_type_count(&trans_arena, VkPresentModeKHR, details.present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_mode_count, details.present_modes);
  }

  return details;
}

} // namespace render
} // namespace polymer
