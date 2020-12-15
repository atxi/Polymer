#include "render.h"

#include "math.h"

#include <cassert>
#include <chrono>
#include <cstdio>

#pragma warning(disable : 26812) // disable unscoped enum warning

namespace polymer {

const char* const kRequiredExtensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_utils"};
const char* const kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
const char* const kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};
#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

struct Vertex {
  Vector2f pos;
  Vector3f color;
};

struct UniformBufferObject {
  mat4 mvp;
};

const Vertex vertices[] = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};

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

SizedString ReadEntireFile(const char* filename, MemoryArena* arena) {
  SizedString result = {};
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

  result.str = buffer;
  result.size = size;

  return result;
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

  CreateDescriptorSetLayout();
  
  CreateVertexBuffer();
  RecreateSwapchain();
  CreateSyncObjects();

  return true;
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

  VkCommandBufferBeginInfo begin_info = {};

  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = nullptr;

  if (vkBeginCommandBuffer(command_buffers[current_frame], &begin_info) != VK_SUCCESS) {
    fprintf(stderr, "Failed ot begin recording command buffer.\n");
    return false;
  }

  VkRenderPassBeginInfo render_pass_info = {};

  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_info.renderPass = render_pass;
  render_pass_info.framebuffer = swap_framebuffers[current_image];
  render_pass_info.renderArea.offset = { 0, 0 };
  render_pass_info.renderArea.extent = swap_extent;

  VkClearValue clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  vkCmdBeginRenderPass(command_buffers[current_frame], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  {
    vkCmdBindPipeline(command_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    VkBuffer vertex_buffers[] = { vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffers[current_frame], 0, 1, vertex_buffers, offsets);
    vkCmdBindDescriptorSets(command_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptor_sets + current_image, 0, nullptr);
    vkCmdDraw(command_buffers[current_frame], polymer_array_count(vertices), 1, 0, 0);
  }

  return true;
}

void VulkanRenderer::Render() {
  u32 image_index = current_image;

  if (image_fences[image_index] != VK_NULL_HANDLE) {
    vkWaitForFences(device, 1, &image_fences[image_index], VK_TRUE, UINT64_MAX);
  }

  image_fences[image_index] = frame_fences[current_frame];

  vkCmdEndRenderPass(command_buffers[current_frame]);

  if (vkEndCommandBuffer(command_buffers[current_frame]) != VK_SUCCESS) {
    fprintf(stderr, "Failed to record command buffer.\n");
  }

  UpdateUniforms(image_index);

  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {image_available_semaphores[current_frame]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = waitSemaphores;
  submit_info.pWaitDstStageMask = waitStages;

  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = command_buffers + current_frame;

  VkSemaphore signal_semaphores[] = {render_finished_semaphores[current_frame]};

  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;

  vkResetFences(device, 1, frame_fences + current_frame);

  if (vkQueueSubmit(graphics_queue, 1, &submit_info, frame_fences[current_frame]) != VK_SUCCESS) {
    fprintf(stderr, "Failed to submit draw command buffer.\n");
  }

  VkSwapchainKHR swapchains[] = {swapchain};

  VkPresentInfoKHR present_info = {};

  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;
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

void VulkanRenderer::UpdateUniforms(u32 index) {
  static auto start_time = std::chrono::high_resolution_clock::now();

  auto current_time = std::chrono::high_resolution_clock::now();

  float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

  UniformBufferObject ubo;

  mat4 model = Rotate(mat4::Identity(), time * Radians(90.0f), Vector3f(0, 0, 1.0f));
  mat4 view = LookAt(Vector3f(2, 2, 2), Vector3f(0, 0, 0), Vector3f(0, 0, 1));
  mat4 proj = Perspective(Radians(45.0f), (float)swap_extent.width / swap_extent.height, 0.1f, 100.0f);

  ubo.mvp = proj * view * model;

  void* data = nullptr;

  vmaMapMemory(allocator, uniform_allocations[index], &data);
  memcpy(data, ubo.mvp.data, sizeof(UniformBufferObject));
  vmaUnmapMemory(allocator, uniform_allocations[index]);
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

  BeginOneShotCommandBuffer();

  vkCmdCopyBuffer(oneshot_command_buffer, staging_buffer, mesh.vertex_buffer, 1, &copy);

  EndOneShotCommandBuffer();

  vmaDestroyBuffer(allocator, staging_buffer, staging_alloc);

  mesh.vertex_buffer_size = size;
  mesh.vertex_count = count;

  return mesh;
}

void VulkanRenderer::FreeMesh(RenderMesh* mesh) {
  vmaDestroyBuffer(allocator, mesh->vertex_buffer, mesh->vertex_allocation);
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

  for (size_t i = 0; i < swap_image_count; ++i) {
    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, uniform_buffers + i, uniform_allocations + i,
                        nullptr) != VK_SUCCESS) {
      printf("Failed to create uniform buffer.\n");
    }
  }
}

void VulkanRenderer::CreateVertexBuffer() {
  VkBufferCreateInfo buffer_info = {};

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = sizeof(vertices[0]) * polymer_array_count(vertices);
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
    return;
  }

  if (staging_alloc_info.pMappedData) {
    memcpy(staging_alloc_info.pMappedData, vertices, (size_t)buffer_info.size);
  }

  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_create_info.flags = 0;

  if (vmaCreateBuffer(allocator, &buffer_info, &alloc_create_info, &vertex_buffer, &vertex_allocation, nullptr) !=
      VK_SUCCESS) {
    printf("Failed to create vertex buffer.\n");
    return;
  }

  VkBufferCopy copy = {};
  copy.srcOffset = 0;
  copy.dstOffset = 0;
  copy.size = buffer_info.size;

  BeginOneShotCommandBuffer();

  vkCmdCopyBuffer(oneshot_command_buffer, staging_buffer, vertex_buffer, 1, &copy);

  EndOneShotCommandBuffer();

  vmaDestroyBuffer(allocator, staging_buffer, staging_alloc);
}

void VulkanRenderer::CreateDescriptorPool() {
  VkDescriptorPoolSize pool_size = {};

  pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_size.descriptorCount = swap_image_count;

  VkDescriptorPoolCreateInfo pool_info = {};

  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  pool_info.maxSets = swap_image_count;

  if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create descriptor pool.\n");
  }
}

void VulkanRenderer::CreateDescriptorSets() {
  VkDescriptorSetLayout layouts[6];

  for (u32 i = 0; i < swap_image_count; ++i) {
    layouts[i] = descriptor_layout;
  }

  VkDescriptorSetAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = descriptor_pool;
  alloc_info.descriptorSetCount = swap_image_count;
  alloc_info.pSetLayouts = layouts;
 
  if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate descriptor sets.");
  }

  for (u32 i = 0; i < swap_image_count; ++i) {
    VkDescriptorBufferInfo buffer_info = {};

    buffer_info.buffer = uniform_buffers[i];
    buffer_info.offset = 0;
    buffer_info.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptor_write = {};
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = descriptor_sets[i];
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pBufferInfo = &buffer_info;
    descriptor_write.pImageInfo = nullptr;
    descriptor_write.pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);
  }
}

void VulkanRenderer::CleanupSwapchain() {
  if (swapchain == VK_NULL_HANDLE || swap_image_count == 0)
    return;

  for (u32 i = 0; i < swap_image_count; i++) {
    vkDestroyFramebuffer(device, swap_framebuffers[i], nullptr);

    vmaDestroyBuffer(allocator, uniform_buffers[i], uniform_allocations[i]);
  }

  vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

  vkFreeCommandBuffers(device, command_pool, kMaxFramesInFlight, command_buffers);

  vkDestroyPipeline(device, graphics_pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
  vkDestroyRenderPass(device, render_pass, nullptr);

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
  CreateImageViews();
  CreateRenderPass();
  CreateUniformBuffers();
  CreateDescriptorPool();
  CreateDescriptorSets();
  CreateGraphicsPipeline();
  CreateFramebuffers();
  CreateCommandBuffers();

  this->render_paused = false;
  this->invalid_swapchain = false;
}

void VulkanRenderer::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphore_info = {};

  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

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
  VkCommandBufferAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = kMaxFramesInFlight;

  if (vkAllocateCommandBuffers(device, &alloc_info, command_buffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers.\n");
  }
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
    VkImageView attachments[] = {swap_image_views[i]};

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = 1;
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
  VkAttachmentDescription color_attachment = {};

  color_attachment.format = swap_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  VkRenderPassCreateInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create render pass.\n");
  }
}

void VulkanRenderer::CreateDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding layout_binding = {};

  layout_binding.binding = 0;
  layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  layout_binding.descriptorCount = 1;
  layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo layout_create_info = {};
  layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_create_info.bindingCount = 1;
  layout_create_info.pBindings = &layout_binding;

  if (vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr, &descriptor_layout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create descriptor set layout.\n");
  }
}

void VulkanRenderer::CreateGraphicsPipeline() {
  SizedString vert_code = ReadEntireFile("shaders/vert.spv", trans_arena);
  SizedString frag_code = ReadEntireFile("shaders/frag.spv", trans_arena);

  if (vert_code.size == 0) {
    fprintf(stderr, "Failed to read vertex shader file.\n");
    return;
  }

  if (frag_code.size == 0) {
    fprintf(stderr, "Failed to read fragment shader file.\n");
    return;
  }

  VkShaderModule vertex_shader = CreateShaderModule(vert_code);
  VkShaderModule frag_shader = CreateShaderModule(frag_code);

  VkPipelineShaderStageCreateInfo vert_shader_create_info = {};
  vert_shader_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_shader_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_shader_create_info.module = vertex_shader;
  vert_shader_create_info.pName = "main";

  VkPipelineShaderStageCreateInfo frag_shader_create_info = {};
  frag_shader_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_shader_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_shader_create_info.module = frag_shader;
  frag_shader_create_info.pName = "main";

  VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_create_info, frag_shader_create_info};

  VkVertexInputBindingDescription binding_description = {};

  binding_description.binding = 0;
  binding_description.stride = sizeof(Vertex);
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribute_descriptions[2];
  attribute_descriptions[0].binding = 0;
  attribute_descriptions[0].location = 0;
  attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attribute_descriptions[0].offset = offsetof(Vertex, pos);

  attribute_descriptions[1].binding = 0;
  attribute_descriptions[1].location = 1;
  attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute_descriptions[1].offset = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
  vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &binding_description;
  vertex_input_info.vertexAttributeDescriptionCount = 2;
  vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)swap_extent.width;
  viewport.height = (float)swap_extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swap_extent;

  VkPipelineViewportStateCreateInfo viewport_state = {};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;

  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = nullptr;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blend_attachment.blendEnable = VK_FALSE;
  blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo blend = {};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.logicOpEnable = VK_FALSE;
  blend.logicOp = VK_LOGIC_OP_COPY;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  blend.blendConstants[0] = 0.0f;
  blend.blendConstants[1] = 0.0f;
  blend.blendConstants[2] = 0.0f;
  blend.blendConstants[3] = 0.0f;

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH};

  VkPipelineDynamicStateCreateInfo dynamic_state = {};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
  pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.setLayoutCount = 1;
  pipeline_layout_create_info.pSetLayouts = &descriptor_layout;
  pipeline_layout_create_info.pushConstantRangeCount = 0;
  pipeline_layout_create_info.pPushConstantRanges = nullptr;

  if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create pipeline layout.\n");
  }

  VkGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;

  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = nullptr;
  pipeline_info.pColorBlendState = &blend;
  pipeline_info.pDynamicState = nullptr;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create graphics pipeline.\n");
  }

  vkDestroyShaderModule(device, vertex_shader, nullptr);
  vkDestroyShaderModule(device, frag_shader, nullptr);
}

VkShaderModule VulkanRenderer::CreateShaderModule(SizedString code) {
  VkShaderModuleCreateInfo create_info{};

  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size;
  create_info.pCode = (u32*)code.str;

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

  return indices.IsComplete() && has_extensions && swapchain_adequate;
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
  if (!kEnableValidationLayers)
    return;

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

  CleanupSwapchain();

  vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);

  vmaDestroyBuffer(allocator, vertex_buffer, vertex_allocation);
  vmaDestroyAllocator(allocator);

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkDestroySemaphore(device, image_available_semaphores[i], nullptr);
    vkDestroySemaphore(device, render_finished_semaphores[i], nullptr);
    vkDestroyFence(device, frame_fences[i], nullptr);
  }

  vkDestroyCommandPool(device, command_pool, nullptr);

  vkDestroySurfaceKHR(instance, surface, nullptr);

  vkDestroyDevice(device, nullptr);

  if (kEnableValidationLayers) {
    DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
  }

  vkDestroyInstance(instance, nullptr);
}

} // namespace polymer
