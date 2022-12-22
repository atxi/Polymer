#ifndef POLYMER_RENDER_RENDER_H_
#define POLYMER_RENDER_RENDER_H_

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

#include "chunk_renderer.h"
#include "font_renderer.h"

#include "../buffer.h"
#include "../math.h"
#include "../memory.h"

// TODO: This entire thing needs to be re-written because it's just hacked together in a way that doesn't allow multiple
// dependent draw submissions easily. It also doesn't support creating any kind of textures except the texture array
// used for block rendering.

namespace polymer {
namespace render {

void CreateRenderPassType(VkDevice device, VkFormat swap_format, VkRenderPass* render_pass,
                          VkAttachmentDescription color_attachment, VkAttachmentDescription depth_attachment);

VkShaderModule CreateShaderModule(VkDevice device, String code);
String ReadEntireFile(const char* filename, MemoryArena* arena);

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
  u32 vertex_count;

  VkBuffer index_buffer;
  VmaAllocation index_allocation;
  u32 index_count;
};

struct TextureArray {
  VmaAllocation allocation;
  VkImage image;
  VkImageView image_view;
  VkSampler sampler;

  u16 mips;
  u16 depth;

  // Width and height must be the same
  // TODO: This shouldn't be required, but the mip generator might need updated.
  u32 dimensions;

  u32 channels;
  VkFormat format;

  TextureArray* next;
  TextureArray* prev;
};

// Stores state for creating a data push command to fill the texture array.
// This allows it to push all of the textures at once for improved performance.
struct TextureArrayPushState {
  enum class Status { Success, ErrorBuffer, Initial };

  TextureArray& texture;

  VkBuffer buffer;
  VmaAllocation alloc;
  VmaAllocationInfo alloc_info;
  Status status;

  // Size of one texture with its mips.
  size_t texture_data_size;

  TextureArrayPushState(TextureArray& texture)
      : texture(texture), buffer(), alloc(), alloc_info(), status(Status::Initial), texture_data_size(0) {}
};

struct TextureArrayManager {
  TextureArray* textures = nullptr;
  TextureArray* last = nullptr;
  TextureArray* free = nullptr;

  TextureArray* CreateTexture(MemoryArena& arena) {
    TextureArray* result = nullptr;

    if (free) {
      result = free;
      free = free->next;
    } else {
      result = memory_arena_push_type(&arena, TextureArray);
    }

    result->next = textures;
    result->prev = nullptr;

    if (textures) {
      textures->prev = result;
    }
    textures = result;

    if (last == nullptr) {
      last = result;
    }

    return result;
  }

  void ReleaseTexture(TextureArray& texture) {
    if (texture.prev) {
      texture.prev->next = texture.next;
    }

    if (texture.next) {
      texture.next->prev = texture.prev;
    }

    if (&texture == last) {
      last = last->prev;
    }
  }

  void Clear() {
    TextureArray* current = textures;

    while (current) {
      TextureArray* texture = current;
      current = current->next;

      texture->next = free;
      free = texture;
    }

    last = nullptr;
    textures = nullptr;
  }
};

struct VulkanRenderer {
  MemoryArena* trans_arena;
  MemoryArena* perm_arena;
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
  VkSampler swap_sampler;
  VkExtent2D swap_extent;

  VmaAllocator allocator;

  VkDescriptorPool descriptor_pool;

  VkCommandPool command_pool;
  VkCommandBuffer oneshot_command_buffer;
  VkSemaphore image_available_semaphores[kMaxFramesInFlight];

  VkFence frame_fences[kMaxFramesInFlight];
  VkFence image_fences[6];

  VkImage depth_image;
  VmaAllocation depth_allocation;
  VkImageView depth_image_view;

  TextureArrayManager texture_array_manager;

  size_t current_frame = 0;
  u32 current_image = 0;
  bool render_paused;
  bool invalid_swapchain;

  ChunkRenderer chunk_renderer;
  FontRenderer font_renderer;

  // A list of staging buffers that need to be freed after pushing the oneshot allocation command buffer.
  VkBuffer staging_buffers[2048];
  VmaAllocation staging_allocs[2048];
  size_t staging_buffer_count = 0;

  bool Initialize(HWND hwnd);
  void RecreateSwapchain();
  bool BeginFrame();

  void Render();
  void Cleanup();

  // Uses staging buffer to push data to the gpu and returns the allocation buffers.
  RenderMesh AllocateMesh(u8* vertex_data, size_t vertex_data_size, size_t vertex_count, u16* index_data,
                          size_t index_count);
  void FreeMesh(RenderMesh* mesh);

  TextureArrayPushState BeginTexturePush(TextureArray& texture);
  void CommitTexturePush(TextureArrayPushState& state);

  TextureArray* CreateTextureArray(size_t width, size_t height, size_t layers, int channels = 4,
                                   bool enable_mips = true);
  void PushArrayTexture(MemoryArena& temp_arena, TextureArrayPushState& state, u8* texture, size_t index);
  void FreeTextureArray(TextureArray& texture);

  void BeginMeshAllocation();
  void EndMeshAllocation();

  void WaitForIdle();

private:
  bool PushStagingBuffer(u8* data, size_t data_size, VkBuffer* buffer, VmaAllocation* allocation,
                         VkBufferUsageFlagBits usage_type);

  u32 FindMemoryType(u32 type_filter, VkMemoryPropertyFlags properties);

  void GenerateArrayMipmaps(TextureArray& texture, u32 index);
  void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout,
                             u32 base_layer, u32 layer_count, u32 mips);
  void CreateDepthBuffer();
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

} // namespace render
} // namespace polymer

#endif
