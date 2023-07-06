#ifndef POLYMER_RENDER_RENDER_H_
#define POLYMER_RENDER_RENDER_H_

#include <polymer/render/render_pass.h>
#include <polymer/render/swapchain.h>
#include <polymer/render/texture.h>
#include <polymer/render/util.h>

#include <polymer/buffer.h>
#include <polymer/math.h>
#include <polymer/memory.h>
#include <polymer/platform/platform.h>

namespace polymer {
namespace render {

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

struct RenderMesh {
  VkBuffer vertex_buffer;
  VmaAllocation vertex_allocation;
  u32 vertex_count;

  VkBuffer index_buffer;
  VmaAllocation index_allocation;
  u32 index_count;
};

struct UniformBuffer {
  VmaAllocator allocator;

  VkBuffer uniform_buffers[kMaxFramesInFlight];
  VmaAllocation uniform_allocations[kMaxFramesInFlight];

  void Create(VmaAllocator allocator, size_t size);

  inline void Destroy() {
    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
      vmaDestroyBuffer(allocator, uniform_buffers[i], uniform_allocations[i]);
    }
  }

  void Set(size_t frame, void* data, size_t data_size);
};

struct DescriptorSet {
  VkDescriptorSet descriptors[kMaxFramesInFlight];

  inline VkDescriptorSet& operator[](size_t index) {
    return descriptors[index];
  }
};

struct VulkanRenderer {
  Platform* platform = nullptr;

  MemoryArena* trans_arena = nullptr;
  MemoryArena* perm_arena = nullptr;
  PolymerWindow hwnd;
  ExtensionRequest extension_request;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical_device;
  VkDevice device;

  Swapchain swapchain;

  VkQueue graphics_queue;
  VkQueue present_queue;

  VmaAllocator allocator;

  // TODO: This should be pulled out to be managed per-thread
  VkDescriptorPool descriptor_pool;
  // TODO: This should be pulled out to be managed per-thread
  VkCommandPool command_pool;

  VkSemaphore render_complete_semaphores[kMaxFramesInFlight];
  VkSemaphore image_available_semaphores[kMaxFramesInFlight];
  VkFence frame_fences[kMaxFramesInFlight];

  TextureArrayManager texture_array_manager;

  size_t current_frame = 0;
  u32 current_image = 0;
  bool render_paused;
  bool invalid_swapchain;

  // TODO: Pull this out into a freelist so multiple oneshots can be built up at once.
  // TODO: This should be pulled out to be managed per-thread
  VkCommandBuffer oneshot_command_buffer;

  // A list of staging buffers that need to be freed after pushing the oneshot allocation command buffer.
  VkBuffer staging_buffers[2048];
  VmaAllocation staging_allocs[2048];
  size_t staging_buffer_count = 0;

  bool Initialize(PolymerWindow window);
  void RecreateSwapchain();
  bool BeginFrame();

  void Render();
  void Shutdown();

  // Uses staging buffer to push data to the gpu and returns the allocation buffers.
  RenderMesh AllocateMesh(u8* vertex_data, size_t vertex_data_size, size_t vertex_count, u16* index_data,
                          size_t index_count);
  void FreeMesh(RenderMesh* mesh);

  TextureArrayPushState BeginTexturePush(TextureArray& texture);
  void CommitTexturePush(TextureArrayPushState& state);

  TextureArray* CreateTextureArray(size_t width, size_t height, size_t layers, int channels = 4,
                                   bool enable_mips = true);
  void PushArrayTexture(MemoryArena& temp_arena, TextureArrayPushState& state, u8* texture, size_t index,
                        const TextureConfig& cfg);
  void FreeTextureArray(TextureArray& texture);

  void BeginMeshAllocation();
  void EndMeshAllocation();

  void WaitForIdle();

  inline const VkExtent2D& GetExtent() const {
    return swapchain.extent;
  }

private:
  bool PushStagingBuffer(u8* data, size_t data_size, VkBuffer* buffer, VmaAllocation* allocation,
                         VkBufferUsageFlagBits usage_type);

  u32 FindMemoryType(u32 type_filter, VkMemoryPropertyFlags properties);

  void GenerateArrayMipmaps(TextureArray& texture, u32 index);
  void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout,
                             u32 base_layer, u32 layer_count, u32 mips);
  void BeginOneShotCommandBuffer();
  void EndOneShotCommandBuffer();

  void CreateSyncObjects();
  void CreateCommandPool();
  void CreateDescriptorPool();

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
