#include "connection.h"
#include "gamestate.h"
#include "inflate.h"
#include "memory.h"
#include "packet_interpreter.h"
#include "types.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable : 26812) // disable unscoped enum warning

namespace polymer {

// Window surface width
constexpr u32 kWidth = 1280;
// Window surface height
constexpr u32 kHeight = 720;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_DESTROY: {
    PostQuitMessage(0);
  } break;
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  return 0;
}

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

struct VulkanRenderer {
  MemoryArena* trans_arena;

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
  VkFormat swap_format;
  VkExtent2D swap_extent;

  bool Initialize(HWND hwnd) {
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
    CreateSwapChain();

    return true;
  }

  void CreateSwapChain() {
    SwapChainSupportDetails swapchain_support = QuerySwapChainSupport(physical_device);

    VkSurfaceFormatKHR surface_format = ChooseSwapSurfaceFormat(swapchain_support.formats, swapchain_support.format_count);
    VkPresentModeKHR present_mode = ChooseSwapPresentMode(swapchain_support.present_modes, swapchain_support.present_mode_count);
    VkExtent2D extent = ChooseSwapExtent(swapchain_support.capabilities);

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
    uint32_t queue_indices[] = { indices.graphics, indices.present };

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

  VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
      return capabilities.currentExtent;
    }

    VkExtent2D extent = {kWidth, kHeight};

    return extent;
  }

  VkPresentModeKHR ChooseSwapPresentMode(VkPresentModeKHR* present_modes, u32 present_mode_count) {
    for (u32 i = 0; i < present_mode_count; ++i) {
      VkPresentModeKHR mode = present_modes[i];

      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return mode;
      }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkSurfaceFormatKHR ChooseSwapSurfaceFormat(VkSurfaceFormatKHR* formats, u32 format_count) {
    for (u32 i = 0; i < format_count; ++i) {
      VkSurfaceFormatKHR* format = formats + i;

      if (format->format == VK_FORMAT_B8G8R8A8_SRGB && format->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return *format;
      }
    }

    return formats[0];
  }

  SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) {
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

  bool CreateWindowSurface(HWND hwnd, VkSurfaceKHR* surface) {
    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hinstance = GetModuleHandle(nullptr);
    surface_info.hwnd = hwnd;

    return vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, surface) == VK_SUCCESS;
  }

  u32 AddUniqueQueue(VkDeviceQueueCreateInfo* infos, u32 count, u32 queue_index) {
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

  void CreateLogicalDevice() {
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

  QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) {
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

  bool IsDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = FindQueueFamilies(device);

    bool has_extensions = DeviceHasExtensions(device);

    bool swapchain_adequate = false;

    if (has_extensions) {
      SwapChainSupportDetails swapchain_details = QuerySwapChainSupport(device);

      swapchain_adequate = swapchain_details.format_count > 0 && swapchain_details.present_mode_count > 0;
    }

    return indices.IsComplete() && has_extensions && swapchain_adequate;
  }

  bool DeviceHasExtensions(VkPhysicalDevice device) {
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

  bool PickPhysicalDevice() {
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

  void SetupDebugMessenger() {
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

  bool CheckValidationLayerSupport() {
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

  bool CreateInstance() {
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

  void Cleanup() {
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);

    vkDestroyDevice(device, nullptr);

    if (kEnableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    }

    vkDestroyInstance(instance, nullptr);
  }
};

VulkanRenderer vk_render;

int run() {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = gigabytes(1);
  constexpr size_t kTransientSize = megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  vk_render.trans_arena = &trans_arena;

  WNDCLASSEX wc = {};

  wc.cbSize = sizeof(wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"polymer";

  if (!RegisterClassEx(&wc)) {
    fprintf(stderr, "Failed to register window.\n");
    exit(1);
  }

  RECT rect = {0, 0, kWidth, kHeight};
  DWORD ex_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_OVERLAPPEDWINDOW;
  AdjustWindowRectEx(&rect, wc.style, FALSE, ex_style);

  u32 window_width = rect.right - rect.left;
  u32 window_height = rect.bottom - rect.top;

  HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"Polymer", ex_style, CW_USEDEFAULT, CW_USEDEFAULT, window_width,
                             window_height, nullptr, nullptr, wc.hInstance, nullptr);

  if (!hwnd) {
    fprintf(stderr, "Failed to create window.\n");
    exit(1);
  }

  vk_render.Initialize(hwnd);

  MSG msg = {};
  bool running = true;
  while (running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);

      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
    }
  }

  vk_render.Cleanup();

  return 0;
  printf("Polymer\n");

  GameState* game = memory_arena_construct_type(&perm_arena, GameState, &perm_arena, &trans_arena);
  PacketInterpreter interpreter(game);
  Connection* connection = &game->connection;

  connection->interpreter = &interpreter;

  game->LoadBlocks();

  // Allocate mirrored ring buffers so they can always be inflated
  connection->read_buffer.size = kMirrorBufferSize;
  connection->read_buffer.data = AllocateMirroredBuffer(connection->read_buffer.size);
  connection->write_buffer.size = kMirrorBufferSize;
  connection->write_buffer.data = AllocateMirroredBuffer(connection->write_buffer.size);

  assert(connection->read_buffer.data);
  assert(connection->write_buffer.data);

  // Inflate buffer doesn't need mirrored because it always operates from byte zero.
  RingBuffer inflate_buffer(perm_arena, kMirrorBufferSize);

  ConnectResult connect_result = connection->Connect("127.0.0.1", 25565);

  switch (connect_result) {
  case ConnectResult::ErrorSocket: {
    fprintf(stderr, "Failed to create socket\n");
    return 1;
  }
  case ConnectResult::ErrorAddrInfo: {
    fprintf(stderr, "Failed to get address info\n");
    return 1;
  }
  case ConnectResult::ErrorConnect: {
    fprintf(stderr, "Failed to connect\n");
    return 1;
  }
  default:
    break;
  }

  printf("Connected to server.\n");

  connection->SetBlocking(false);

  connection->SendHandshake(754, "127.0.0.1", 25565, ProtocolState::Login);
  connection->SendLoginStart("polymer");

  while (connection->connected) {
    trans_arena.Reset();

    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }
  }

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
