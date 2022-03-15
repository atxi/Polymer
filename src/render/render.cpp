#include "render.h"

#include "../math.h"

#include <cassert>
#include <cstdio>

#pragma warning(disable : 26812) // disable unscoped enum warning

namespace polymer {
namespace render {

const char* const kRequiredExtensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_utils"};
const char* const kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
const char* const kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};
#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

static VkBool32 VKAPI_PTR DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
  fprintf(stderr, "Validation: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }

  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

void CreateRenderPassType(VkDevice device, VkFormat swap_format, VkRenderPass* render_pass,
                          VkAttachmentDescription color_attachment, VkAttachmentDescription depth_attachment) {
  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depth_attachment_ref = {};
  depth_attachment_ref.attachment = 1;
  depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.pDepthStencilAttachment = &depth_attachment_ref;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = polymer_array_count(attachments);
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &render_pass_info, nullptr, render_pass) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create render pass.\n");
  }
}

String ReadEntireFile(const char* filename, MemoryArena* arena) {
  String result = {};
  FILE* f = fopen(filename, "rb");

  if (!f) {
    return result;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(arena, char, size);
  fread(buffer, 1, size, f);
  fclose(f);

  result.data = buffer;
  result.size = size;

  return result;
}

struct Mipmap {
  unsigned char* data;
  size_t dimension;

  Mipmap(unsigned char* data, size_t dimension) : data(data), dimension(dimension) {}

  int Sample(size_t x, size_t y, size_t color_offset) {
    return data[(y * dimension + x) * 4 + color_offset];
  }
};

inline float GetColorGamma(int a, int b, int c, int d) {
  float an = a / 255.0f;
  float bn = b / 255.0f;
  float cn = c / 255.0f;
  float dn = d / 255.0f;

  return (powf(an, 2.2f) + powf(bn, 2.2f) + powf(cn, 2.2f) + powf(dn, 2.2f)) / 4.0f;
}

// Blend four samples into a final result after doing gamma conversions
inline int GammaBlend(int a, int b, int c, int d) {
  float result = powf(GetColorGamma(a, b, c, d), 1.0f / 2.2f);

  return static_cast<int>(255.0f * result);
}

// Performs basic pixel averaging filter for generating mipmap.
void BoxFilterMipmap(u8* previous, u8* data, size_t data_size, size_t dim) {
  size_t size_per_tex = dim * dim * 4;
  size_t count = data_size / size_per_tex;
  size_t prev_dim = dim * 2;

  unsigned int* pixel = (unsigned int*)data;
  for (size_t i = 0; i < count; ++i) {
    unsigned char* prev_tex = previous + i * (prev_dim * prev_dim * 4);

    Mipmap source(prev_tex, prev_dim);

    for (size_t y = 0; y < dim; ++y) {
      for (size_t x = 0; x < dim; ++x) {
        int red, green, blue, alpha;

        const size_t red_index = 0;
        const size_t green_index = 1;
        const size_t blue_index = 2;
        const size_t alpha_index = 3;

        red = GammaBlend(source.Sample(x * 2, y * 2, red_index), source.Sample(x * 2 + 1, y * 2, red_index),
                         source.Sample(x * 2, y * 2 + 1, red_index), source.Sample(x * 2 + 1, y * 2 + 1, red_index));

        green =
            GammaBlend(source.Sample(x * 2, y * 2, green_index), source.Sample(x * 2 + 1, y * 2, green_index),
                       source.Sample(x * 2, y * 2 + 1, green_index), source.Sample(x * 2 + 1, y * 2 + 1, green_index));

        blue = GammaBlend(source.Sample(x * 2, y * 2, blue_index), source.Sample(x * 2 + 1, y * 2, blue_index),
                          source.Sample(x * 2, y * 2 + 1, blue_index), source.Sample(x * 2 + 1, y * 2 + 1, blue_index));

        alpha =
            GammaBlend(source.Sample(x * 2, y * 2, alpha_index), source.Sample(x * 2 + 1, y * 2, alpha_index),
                       source.Sample(x * 2, y * 2 + 1, alpha_index), source.Sample(x * 2 + 1, y * 2 + 1, alpha_index));

        // AA BB GG RR
        *pixel = ((alpha & 0xFF) << 24) | ((blue & 0xFF) << 16) | ((green & 0xFF) << 8) | (red & 0xFF);
        ++pixel;
      }
    }
  }
}

bool VulkanRenderer::Initialize(HWND hwnd) {
  this->hwnd = hwnd;
  this->render_paused = false;
  this->invalid_swapchain = false;

  swapchain = VK_NULL_HANDLE;

  if (!CreateInstance()) {
    return false;
  }

  SetupDebugMessenger();

  if (!CreateWindowSurface(hwnd, &surface)) {
    fprintf(stderr, "Failed to create window surface.\n");
    return false;
  }

  PickPhysicalDevice();
  CreateLogicalDevice();

  CreateCommandPool();

  VkCommandBufferAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(device, &alloc_info, &oneshot_command_buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate oneshot command buffer.\n");
    return false;
  }

  VmaAllocatorCreateInfo allocator_info = {};

  allocator_info.vulkanApiVersion = VK_API_VERSION_1_0;
  allocator_info.physicalDevice = physical_device;
  allocator_info.device = device;
  allocator_info.instance = instance;

  if (vmaCreateAllocator(&allocator_info, &allocator) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create vma allocator.\n");
    return false;
  }

  return true;
}

void VulkanRenderer::CreateTexture(size_t width, size_t height, size_t layers) {
  VkImageCreateInfo image_info = {};

  texture_mips = (u32)std::floor(std::log2(width)) + 1;

  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.extent.width = (u32)width;
  image_info.extent.height = (u32)height;
  image_info.extent.depth = 1;
  image_info.mipLevels = texture_mips;
  image_info.arrayLayers = (u32)layers;
  image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.flags = 0;

  if (vkCreateImage(device, &image_info, nullptr, &texture_image) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture image.\n");
  }

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_create_info.flags = 0;

  if (vmaAllocateMemoryForImage(allocator, texture_image, &alloc_create_info, &texture_allocation, nullptr) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate memory for texture image.\n");
  }

  vmaBindImageMemory(allocator, texture_allocation, texture_image);

  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.image = texture_image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  view_create_info.format = VK_FORMAT_R8G8B8A8_SRGB;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = image_info.mipLevels;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = (u32)layers;

  if (vkCreateImageView(device, &view_create_info, nullptr, &texture_image_view) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture image view.\n");
  }

  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(physical_device, &properties);

  VkSamplerCreateInfo sampler_info = {};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.anisotropyEnable = VK_TRUE;
  sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  sampler_info.unnormalizedCoordinates = VK_FALSE;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = (float)texture_mips;

  if (vkCreateSampler(device, &sampler_info, nullptr, &texture_sampler) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture sampler.\n");
  }
}

void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout,
                                           VkImageLayout new_layout, u32 layer) {

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = texture_mips;
  barrier.subresourceRange.baseArrayLayer = layer;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = 0;

  VkPipelineStageFlags source_stage, destination_stage;

  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    fprintf(stderr, "Unsupported image layout transition.\n");
    return;
  }

  BeginOneShotCommandBuffer();

  vkCmdPipelineBarrier(oneshot_command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

  EndOneShotCommandBuffer();
}

void VulkanRenderer::PushTexture(MemoryArena& temp_arena, u8* texture, size_t index) {
  if (texture == nullptr) {
    TransitionImageLayout(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (u32)index);
    TransitionImageLayout(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, (u32)index);
    return;
  }

  // Transition image to copy-destination optimal, then copy, then transition to shader-read optimal.
  TransitionImageLayout(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (u32)index);

  u32 dim = 16;

  ArenaSnapshot snapshot = temp_arena.GetSnapshot();

  // Create a buffer that can hold any size mipmap
  u8* previous_data = temp_arena.Allocate(dim * dim * 4);
  u8* buffer_data = temp_arena.Allocate(dim * dim * 4);

  memcpy(previous_data, texture, dim * dim * 4);
  memcpy(buffer_data, texture, dim * dim * 4);

  for (size_t i = 0; i < texture_mips; ++i) {
    BeginOneShotCommandBuffer();

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = VK_NULL_HANDLE;
    VmaAllocationInfo staging_alloc_info = {};

    VkBufferCreateInfo buffer_info = {};

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = dim * dim * 4;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_create_info = {};
    alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    alloc_create_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, &staging_buffer, &staging_alloc,
                        &staging_alloc_info) != VK_SUCCESS) {
      printf("Failed to create staging buffer for texture push.\n");
      return;
    }

    if (staging_alloc_info.pMappedData) {
      if (i > 0) {
        BoxFilterMipmap(previous_data, buffer_data, buffer_info.size, dim);
      }

      memcpy(staging_alloc_info.pMappedData, buffer_data, buffer_info.size);
      memcpy(previous_data, buffer_data, buffer_info.size);
    }

    VkBufferImageCopy region = {};

    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = (u32)i;
    region.imageSubresource.baseArrayLayer = (u32)index;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {dim, dim, 1};

    vkCmdCopyBufferToImage(oneshot_command_buffer, staging_buffer, texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    dim /= 2;

    EndOneShotCommandBuffer();
    vmaDestroyBuffer(allocator, staging_buffer, staging_alloc);
  }

  temp_arena.Revert(snapshot);

  TransitionImageLayout(texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, (u32)index);
}

void VulkanRenderer::GenerateMipmaps(u32 index) {
  VkImageMemoryBarrier barrier = {};

  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = texture_image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = index;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  BeginOneShotCommandBuffer();

  s32 width = 16;
  s32 height = 16;

  for (u32 i = 1; i < texture_mips; ++i) {
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(oneshot_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    VkImageBlit blit = {};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {width, height, 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.baseArrayLayer = index;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {width > 1 ? width / 2 : 1, height > 1 ? height / 2 : 1, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.baseArrayLayer = index;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(oneshot_command_buffer, texture_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(oneshot_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (width > 1) {
      width /= 2;
    }

    if (height > 1) {
      height /= 2;
    }
  }

  barrier.subresourceRange.baseMipLevel = texture_mips - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(oneshot_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                       0, nullptr, 0, nullptr, 1, &barrier);

  EndOneShotCommandBuffer();
}

void VulkanRenderer::CreateDepthBuffer() {
  VkImageCreateInfo image_create_info = {};

  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.extent.width = swap_extent.width;
  image_create_info.extent.height = swap_extent.height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.format = VK_FORMAT_D32_SFLOAT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;

  if (vkCreateImage(device, &image_create_info, nullptr, &depth_image) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create depth buffer image.\n");
  }

  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.image = depth_image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = VK_FORMAT_D32_SFLOAT;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_create_info.flags = 0;

  if (vmaAllocateMemoryForImage(allocator, depth_image, &alloc_create_info, &depth_allocation, nullptr) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate memory for depth buffer.\n");
  }

  vmaBindImageMemory(allocator, depth_allocation, depth_image);

  if (vkCreateImageView(device, &view_create_info, nullptr, &depth_image_view) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create depth image view.\n");
  }
}

bool VulkanRenderer::BeginFrame() {
  vkWaitForFences(device, 1, frame_fences + current_frame, VK_TRUE, UINT64_MAX);

  if (render_paused || invalid_swapchain) {
    RecreateSwapchain();
    return false;
  }

  u32 image_index;

  VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphores[current_frame],
                                          VK_NULL_HANDLE, &image_index);

  current_image = image_index;

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    RecreateSwapchain();
    return false;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    fprintf(stderr, "Failed to acquire swapchain image.\n");
    return false;
  }

  VkRenderPassBeginInfo render_pass_info = {};

  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_info.framebuffer = swap_framebuffers[current_image];
  render_pass_info.renderArea.offset = {0, 0};
  render_pass_info.renderArea.extent = swap_extent;

  chunk_renderer.BeginFrame(render_pass_info, current_frame, pipeline_layout, descriptor_sets[current_frame]);

  return true;
}

void VulkanRenderer::Render() {
  u32 image_index = current_image;

  if (image_fences[image_index] != VK_NULL_HANDLE) {
    vkWaitForFences(device, 1, &image_fences[image_index], VK_TRUE, UINT64_MAX);
  }

  image_fences[image_index] = frame_fences[current_frame];

  VkSemaphore finished_signal_semaphores[] = {render_finished_semaphores[current_frame]};

  VkSemaphore image_sema = image_available_semaphores[current_frame];
  VkSemaphore end_sema = render_finished_semaphores[current_frame];

  chunk_renderer.SubmitCommands(device, graphics_queue, current_frame, image_sema, end_sema,
                                frame_fences[current_frame]);

  VkSwapchainKHR swapchains[] = {swapchain};

  VkPresentInfoKHR present_info = {};

  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = finished_signal_semaphores;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchains;
  present_info.pImageIndices = &image_index;
  present_info.pResults = nullptr;

  VkResult result = vkQueuePresentKHR(present_queue, &present_info);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    RecreateSwapchain();
  } else if (result != VK_SUCCESS) {
    fprintf(stderr, "Failed to present swapchain image.\n");
  }

  current_frame = (current_frame + 1) % kMaxFramesInFlight;
}

u32 VulkanRenderer::FindMemoryType(u32 type_filter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memory_properties;

  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

  for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
    if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  fprintf(stderr, "Failed to find suitable memory type.\n");
  return 0;
}

void VulkanRenderer::BeginOneShotCommandBuffer() {
  VkCommandBufferBeginInfo begin_info = {};

  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = nullptr;

  if (vkBeginCommandBuffer(oneshot_command_buffer, &begin_info) != VK_SUCCESS) {
    fprintf(stderr, "Failed to begin recording oneshot command buffer.\n");
  }
}

void VulkanRenderer::EndOneShotCommandBuffer() {
  if (vkEndCommandBuffer(oneshot_command_buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to record oneshot command buffer.\n");
  }

  VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &oneshot_command_buffer;

  if (vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
    fprintf(stderr, "Failed to submit oneshot command buffer.\n");
  }

  vkQueueWaitIdle(graphics_queue);
}

void VulkanRenderer::WaitForIdle() {
  vkQueueWaitIdle(graphics_queue);
}

RenderMesh VulkanRenderer::AllocateMesh(u8* data, size_t size, size_t count) {
  RenderMesh mesh = {};

  VkBufferCreateInfo buffer_info = {};

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
  alloc_create_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VkBuffer staging_buffer = VK_NULL_HANDLE;
  VmaAllocation staging_alloc = VK_NULL_HANDLE;
  VmaAllocationInfo staging_alloc_info = {};

  if (vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, &staging_buffer, &staging_alloc,
                      &staging_alloc_info) != VK_SUCCESS) {
    printf("Failed to create staging buffer.\n");
    return mesh;
  }

  assert(staging_buffer_count < polymer_array_count(staging_buffers));

  staging_buffers[staging_buffer_count] = staging_buffer;
  staging_allocs[staging_buffer_count] = staging_alloc;

  staging_buffer_count++;

  if (staging_alloc_info.pMappedData) {
    memcpy(staging_alloc_info.pMappedData, data, (size_t)buffer_info.size);
  }

  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_create_info.flags = 0;

  if (vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, &mesh.vertex_buffer, &mesh.vertex_allocation,
                      nullptr) != VK_SUCCESS) {
    printf("Failed to create vertex buffer.\n");
    return mesh;
  }

  VkBufferCopy copy = {};
  copy.srcOffset = 0;
  copy.dstOffset = 0;
  copy.size = buffer_info.size;

  vkCmdCopyBuffer(oneshot_command_buffer, staging_buffer, mesh.vertex_buffer, 1, &copy);

  mesh.vertex_count = (u32)count;

  return mesh;
}

void VulkanRenderer::BeginMeshAllocation() {
  BeginOneShotCommandBuffer();
}

void VulkanRenderer::EndMeshAllocation() {
  EndOneShotCommandBuffer();

  for (size_t i = 0; i < staging_buffer_count; ++i) {
    vmaDestroyBuffer(allocator, staging_buffers[i], staging_allocs[i]);
  }

  staging_buffer_count = 0;
}

void VulkanRenderer::FreeMesh(RenderMesh* mesh) {
  if (mesh->vertex_count > 0) {
    vmaDestroyBuffer(allocator, mesh->vertex_buffer, mesh->vertex_allocation);
  }
}

void VulkanRenderer::CreateUniformBuffers() {
  VkBufferCreateInfo buffer_info = {};

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = sizeof(UniformBufferObject);
  buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
  alloc_create_info.flags = 0;

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, uniform_buffers + i, uniform_allocations + i,
                        nullptr) != VK_SUCCESS) {
      printf("Failed to create uniform buffer.\n");
    }
  }
}

void VulkanRenderer::CreateDescriptorPool() {
  VkDescriptorPoolSize pool_sizes[2] = {};

  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_sizes[0].descriptorCount = swap_image_count;

  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[1].descriptorCount = swap_image_count;

  VkDescriptorPoolCreateInfo pool_info = {};

  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.poolSizeCount = polymer_array_count(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  pool_info.maxSets = swap_image_count;

  if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create descriptor pool.\n");
  }
}

void VulkanRenderer::CreateDescriptorSets() {
  VkDescriptorSetLayout layouts[kMaxFramesInFlight];

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    layouts[i] = descriptor_layout;
  }

  VkDescriptorSetAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = descriptor_pool;
  alloc_info.descriptorSetCount = kMaxFramesInFlight;
  alloc_info.pSetLayouts = layouts;

  if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate descriptor sets.");
  }

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    VkDescriptorBufferInfo buffer_info = {};

    buffer_info.buffer = uniform_buffers[i];
    buffer_info.offset = 0;
    buffer_info.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = texture_image_view;
    image_info.sampler = texture_sampler;

    VkWriteDescriptorSet descriptor_writes[2] = {};
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = descriptor_sets[i];
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = descriptor_sets[i];
    descriptor_writes[1].dstBinding = 1;
    descriptor_writes[1].dstArrayElement = 0;
    descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[1].descriptorCount = 1;
    descriptor_writes[1].pImageInfo = &image_info;
    descriptor_writes[1].pBufferInfo = nullptr;
    descriptor_writes[1].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(device, polymer_array_count(descriptor_writes), descriptor_writes, 0, nullptr);
  }
}

void VulkanRenderer::CleanupSwapchain() {
  if (swapchain == VK_NULL_HANDLE || swap_image_count == 0) return;

  vmaFreeMemory(allocator, depth_allocation);
  vkDestroyImageView(device, depth_image_view, nullptr);
  vkDestroyImage(device, depth_image, nullptr);

  chunk_renderer.CleanupSwapchain(device);

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkDestroySemaphore(device, image_available_semaphores[i], nullptr);
    vkDestroySemaphore(device, render_finished_semaphores[i], nullptr);
    vkDestroyFence(device, frame_fences[i], nullptr);
  }

  for (u32 i = 0; i < swap_image_count; i++) {
    vkDestroyFramebuffer(device, swap_framebuffers[i], nullptr);
  }

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    vmaDestroyBuffer(allocator, uniform_buffers[i], uniform_allocations[i]);
  }

  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

  chunk_renderer.Destroy(device, command_pool);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);

  for (u32 i = 0; i < swap_image_count; i++) {
    vkDestroyImageView(device, swap_image_views[i], nullptr);
  }

  vkDestroySwapchainKHR(device, swapchain, nullptr);
}

void VulkanRenderer::RecreateSwapchain() {
  vkDeviceWaitIdle(device);

  RECT rect;
  GetClientRect(hwnd, &rect);

  if (rect.right - rect.left == 0 || rect.bottom - rect.top == 0) {
    this->render_paused = true;
    return;
  }

  CleanupSwapchain();

  CreateSwapchain();
  CreateDepthBuffer();
  CreateImageViews();
  CreateRenderPass();
  CreateUniformBuffers();
  CreateDescriptorPool();
  CreateDescriptorSets();
  CreateGraphicsPipeline();
  CreateFramebuffers();
  CreateCommandBuffers();
  CreateSyncObjects();

  this->render_paused = false;
  this->invalid_swapchain = false;
}

void VulkanRenderer::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphore_info = {};

  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  chunk_renderer.CreateSyncObjects(device);

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (vkCreateSemaphore(device, &semaphore_info, nullptr, image_available_semaphores + i) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphore_info, nullptr, render_finished_semaphores + i) != VK_SUCCESS) {

      fprintf(stderr, "Failed to create semaphores.\n");
    }

    if (vkCreateFence(device, &fence_info, nullptr, frame_fences + i) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create frame fence.\n");
    }
  }

  for (u32 i = 0; i < swap_image_count; ++i) {
    image_fences[i] = VK_NULL_HANDLE;
  }
}

void VulkanRenderer::CreateCommandBuffers() {
  chunk_renderer.CreateCommandBuffers(device, command_pool);
}

void VulkanRenderer::CreateCommandPool() {
  QueueFamilyIndices indices = FindQueueFamilies(physical_device);

  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.queueFamilyIndex = indices.graphics;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create command pool.\n");
  }
}

void VulkanRenderer::CreateFramebuffers() {
  for (u32 i = 0; i < swap_image_count; i++) {
    VkImageView attachments[] = {swap_image_views[i], depth_image_view};

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = chunk_renderer.block_renderer.render_pass;
    framebuffer_info.attachmentCount = polymer_array_count(attachments);
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = swap_extent.width;
    framebuffer_info.height = swap_extent.height;
    framebuffer_info.layers = 1;

    if (vkCreateFramebuffer(device, &framebuffer_info, nullptr, &swap_framebuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create framebuffer.\n");
    }
  }
}

void VulkanRenderer::CreateRenderPass() {
  chunk_renderer.CreateRenderPass(device, swap_format);
}

void VulkanRenderer::CreateDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding ubo_binding = {};

  ubo_binding.binding = 0;
  ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  ubo_binding.descriptorCount = 1;
  ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutBinding sampler_binding{};
  sampler_binding.binding = 1;
  sampler_binding.descriptorCount = 1;
  sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  sampler_binding.pImmutableSamplers = nullptr;
  sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding layout_bindings[] = {ubo_binding, sampler_binding};

  VkDescriptorSetLayoutCreateInfo layout_create_info = {};
  layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_create_info.bindingCount = polymer_array_count(layout_bindings);
  layout_create_info.pBindings = layout_bindings;

  if (vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr, &descriptor_layout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create descriptor set layout.\n");
  }
}

void VulkanRenderer::CreateGraphicsPipeline() {
  String vert_code = ReadEntireFile("shaders/vert.spv", trans_arena);
  String frag_code = ReadEntireFile("shaders/frag.spv", trans_arena);

  if (vert_code.size == 0) {
    fprintf(stderr, "Failed to read vertex shader file.\n");
    return;
  }

  if (frag_code.size == 0) {
    fprintf(stderr, "Failed to read fragment shader file.\n");
    return;
  }

  VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
  pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.setLayoutCount = 1;
  pipeline_layout_create_info.pSetLayouts = &descriptor_layout;
  pipeline_layout_create_info.pushConstantRangeCount = 0;
  pipeline_layout_create_info.pPushConstantRanges = nullptr;

  if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create pipeline layout.\n");
  }

  VkShaderModule vertex_shader = CreateShaderModule(vert_code);
  VkShaderModule frag_shader = CreateShaderModule(frag_code);

  chunk_renderer.CreatePipeline(device, vertex_shader, frag_shader, swap_extent, pipeline_layout);

  vkDestroyShaderModule(device, vertex_shader, nullptr);
  vkDestroyShaderModule(device, frag_shader, nullptr);
}

VkShaderModule VulkanRenderer::CreateShaderModule(String code) {
  VkShaderModuleCreateInfo create_info{};

  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size;
  create_info.pCode = (u32*)code.data;

  VkShaderModule shader;

  if (vkCreateShaderModule(device, &create_info, nullptr, &shader) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create shader module.\n");
  }

  return shader;
}

void VulkanRenderer::CreateImageViews() {
  for (u32 i = 0; i < swap_image_count; ++i) {
    VkImageViewCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = swap_images[i];
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = swap_format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &create_info, nullptr, &swap_image_views[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create SwapChain image view.\n");
    }
  }
}

void VulkanRenderer::CreateSwapchain() {
  SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(physical_device);

  VkSurfaceFormatKHR surface_format =
      ChooseSwapSurfaceFormat(swapchain_support.formats, swapchain_support.format_count);
  VkPresentModeKHR present_mode =
      ChooseSwapPresentMode(swapchain_support.present_modes, swapchain_support.present_mode_count);
  VkExtent2D extent = ChooseSwapExtent(swapchain_support.capabilities);

  if (extent.width == 0 || extent.height == 0) {
    swapchain = VK_NULL_HANDLE;
    swap_image_count = 0;
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

  QueueFamilyIndices indices = FindQueueFamilies(physical_device);
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

  vkGetSwapchainImagesKHR(device, swapchain, &swap_image_count, nullptr);
  assert(swap_image_count < polymer_array_count(swap_images));
  vkGetSwapchainImagesKHR(device, swapchain, &swap_image_count, swap_images);

  swap_format = create_info.imageFormat;
  swap_extent = create_info.imageExtent;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  RECT rect;
  GetClientRect(hwnd, &rect);

  u32 width = (u32)(rect.right - rect.left);
  u32 height = (u32)(rect.bottom - rect.top);

  VkExtent2D extent = {width, height};

  return extent;
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(VkPresentModeKHR* present_modes, u32 present_mode_count) {
  for (u32 i = 0; i < present_mode_count; ++i) {
    VkPresentModeKHR mode = present_modes[i];

    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(VkSurfaceFormatKHR* formats, u32 format_count) {
  for (u32 i = 0; i < format_count; ++i) {
    VkSurfaceFormatKHR* format = formats + i;

    if (format->format == VK_FORMAT_B8G8R8A8_SRGB && format->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return *format;
    }
  }

  return formats[0];
}

SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) {
  SwapChainSupportDetails details = {};

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count, nullptr);

  if (details.format_count != 0) {
    details.formats = memory_arena_push_type_count(trans_arena, VkSurfaceFormatKHR, details.format_count);

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.format_count, details.formats);
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_mode_count, nullptr);

  if (details.present_mode_count != 0) {
    details.present_modes = memory_arena_push_type_count(trans_arena, VkPresentModeKHR, details.present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_mode_count, details.present_modes);
  }

  return details;
}

bool VulkanRenderer::CreateWindowSurface(HWND hwnd, VkSurfaceKHR* surface) {
  VkWin32SurfaceCreateInfoKHR surface_info = {};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.hinstance = GetModuleHandle(nullptr);
  surface_info.hwnd = hwnd;

  return vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, surface) == VK_SUCCESS;
}

u32 VulkanRenderer::AddUniqueQueue(VkDeviceQueueCreateInfo* infos, u32 count, u32 queue_index) {
  for (u32 i = 0; i < count; ++i) {
    if (infos[i].queueFamilyIndex == queue_index) {
      return count;
    }
  }

  VkDeviceQueueCreateInfo* queue_create_info = infos + count;

  queue_create_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info->queueFamilyIndex = queue_index;
  queue_create_info->queueCount = 1;
  queue_create_info->flags = 0;
  queue_create_info->pNext = 0;

  return ++count;
}

void VulkanRenderer::CreateLogicalDevice() {
  QueueFamilyIndices indices = FindQueueFamilies(physical_device);

  u32 create_count = 0;
  VkDeviceQueueCreateInfo queue_create_infos[12];

  float priority = 1.0f;

  create_count = AddUniqueQueue(queue_create_infos, create_count, indices.graphics);
  create_count = AddUniqueQueue(queue_create_infos, create_count, indices.present);

  for (u32 i = 0; i < create_count; ++i) {
    queue_create_infos[i].pQueuePriorities = &priority;
  }

  VkPhysicalDeviceFeatures features = {};

  features.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pQueueCreateInfos = queue_create_infos;
  create_info.queueCreateInfoCount = create_count;
  create_info.pEnabledFeatures = &features;

  create_info.enabledExtensionCount = polymer_array_count(kDeviceExtensions);
  create_info.ppEnabledExtensionNames = kDeviceExtensions;

  if (kEnableValidationLayers) {
    create_info.enabledLayerCount = polymer_array_count(kValidationLayers);
    create_info.ppEnabledLayerNames = kValidationLayers;
  } else {
    create_info.enabledLayerCount = 0;
  }

  if (vkCreateDevice(physical_device, &create_info, nullptr, &device) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create logical device.\n");
  }

  vkGetDeviceQueue(device, indices.graphics, 0, &graphics_queue);
  vkGetDeviceQueue(device, indices.present, 0, &present_queue);
}

QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices = {};

  u32 count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

  VkQueueFamilyProperties* properties = memory_arena_push_type_count(trans_arena, VkQueueFamilyProperties, count);

  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);

  for (u32 i = 0; i < count; ++i) {
    VkQueueFamilyProperties family = properties[i];

    if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.has_graphics = true;
      indices.graphics = i;
    }

    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

    if (present_support) {
      indices.has_present = true;
      indices.present = i;
    }

    if (indices.IsComplete()) {
      break;
    }
  }

  return indices;
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) {
  QueueFamilyIndices indices = FindQueueFamilies(device);

  bool has_extensions = DeviceHasExtensions(device);

  bool swapchain_adequate = false;

  if (has_extensions) {
    SwapChainSupportDetails swapchain_details = QuerySwapChainSupport(device);

    swapchain_adequate = swapchain_details.format_count > 0 && swapchain_details.present_mode_count > 0;
  }

  VkPhysicalDeviceFeatures features;
  vkGetPhysicalDeviceFeatures(device, &features);

  return indices.IsComplete() && has_extensions && swapchain_adequate && features.samplerAnisotropy;
}

bool VulkanRenderer::DeviceHasExtensions(VkPhysicalDevice device) {
  u32 extension_count;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

  VkExtensionProperties* available_extensions =
      memory_arena_push_type_count(trans_arena, VkExtensionProperties, extension_count);

  vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions);

  for (size_t i = 0; i < polymer_array_count(kDeviceExtensions); ++i) {
    bool found = false;

    for (size_t j = 0; j < extension_count; ++j) {
      if (strcmp(available_extensions[j].extensionName, kDeviceExtensions[i]) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      return false;
    }
  }

  return true;
}

bool VulkanRenderer::PickPhysicalDevice() {
  physical_device = VK_NULL_HANDLE;
  u32 device_count = 0;

  vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

  if (device_count == 0) {
    fprintf(stderr, "Failed to find gpu with Vulkan support\n");
    return false;
  }

  VkPhysicalDevice* devices = memory_arena_push_type_count(trans_arena, VkPhysicalDevice, device_count);

  vkEnumeratePhysicalDevices(instance, &device_count, devices);

  for (size_t i = 0; i < device_count; ++i) {
    VkPhysicalDevice device = devices[i];

    if (IsDeviceSuitable(device)) {
      physical_device = device;
      break;
    }
  }

  if (physical_device == VK_NULL_HANDLE) {
    fprintf(stderr, "Failed to find suitable physical device.\n");
    return false;
  }

  return true;
}

void VulkanRenderer::SetupDebugMessenger() {
  if (!kEnableValidationLayers) return;

  VkDebugUtilsMessengerCreateInfoEXT create_info{};

  create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity = // VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = DebugCallback;
  create_info.pUserData = nullptr;

  if (CreateDebugUtilsMessengerEXT(instance, &create_info, nullptr, &debug_messenger) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create debug messenger.\n");
  }
}

bool VulkanRenderer::CheckValidationLayerSupport() {
  u32 count;

  vkEnumerateInstanceLayerProperties(&count, nullptr);

  VkLayerProperties* properties = memory_arena_push_type_count(trans_arena, VkLayerProperties, count);
  vkEnumerateInstanceLayerProperties(&count, properties);

  for (size_t i = 0; i < polymer_array_count(kValidationLayers); ++i) {
    const char* name = kValidationLayers[i];
    bool found = false;

    for (size_t j = 0; j < count; ++j) {
      VkLayerProperties& property = properties[j];

      if (strcmp(name, property.layerName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      return false;
    }
  }

  return true;
}

bool VulkanRenderer::CreateInstance() {
  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pNext = NULL;
  app_info.pApplicationName = "polymer_instance";
  app_info.applicationVersion = 1;
  app_info.pEngineName = "polymer_instance";
  app_info.engineVersion = 1;
  app_info.apiVersion = VK_API_VERSION_1_0;

  if (kEnableValidationLayers && !CheckValidationLayerSupport()) {
    fprintf(stderr, "Validation layers not available\n");
    return false;
  }

  VkInstanceCreateInfo inst_info = {};
  inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_info.pNext = NULL;
  inst_info.flags = 0;
  inst_info.pApplicationInfo = &app_info;
  inst_info.enabledExtensionCount = polymer_array_count(kRequiredExtensions);
  inst_info.ppEnabledExtensionNames = kRequiredExtensions;
  if (kEnableValidationLayers) {
    inst_info.enabledLayerCount = polymer_array_count(kValidationLayers);
    inst_info.ppEnabledLayerNames = kValidationLayers;
  } else {
    inst_info.enabledLayerCount = 0;
  }

  VkResult res;

  res = vkCreateInstance(&inst_info, NULL, &instance);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "Error creating Vulkan instance: %d\n", res);
    return false;
  }

  return true;
}

void VulkanRenderer::Cleanup() {
  vkDeviceWaitIdle(device);

  vkDestroySampler(device, texture_sampler, nullptr);
  vkDestroyImageView(device, texture_image_view, nullptr);
  vmaDestroyImage(allocator, texture_image, texture_allocation);
  CleanupSwapchain();

  vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);

  vmaDestroyAllocator(allocator);

  vkDestroyCommandPool(device, command_pool, nullptr);

  vkDestroySurfaceKHR(instance, surface, nullptr);

  vkDestroyDevice(device, nullptr);

  if (kEnableValidationLayers) {
    DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
  }

  vkDestroyInstance(instance, nullptr);
}

} // namespace render
} // namespace polymer
