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
};

struct VulkanRenderer {
  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue graphics_queue;

  VkDebugUtilsMessengerEXT debug_messenger;

  bool Initialize(HWND hwnd) {
    if (!CreateInstance()) {
      return false;
    }

    SetupDebugMessenger();
    PickPhysicalDevice();
    CreateLogicalDevice();

    if (!CreateWindowSurface(hwnd, &surface)) {
      fprintf(stderr, "Failed to create window surface.\n");
      return false;
    }

    return true;
  }

  bool CreateWindowSurface(HWND hwnd, VkSurfaceKHR* surface) {
    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hinstance = GetModuleHandle(nullptr);
    surface_info.hwnd = hwnd;

    VkResult result = vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, surface);

    return result == VK_SUCCESS;
  }

  void CreateLogicalDevice() {
    QueueFamilyIndices indices = FindQueueFamilies(physical_device);

    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = indices.graphics;
    queue_create_info.queueCount = 1;

    float priority = 1.0f;
    queue_create_info.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.queueCreateInfoCount = 1;
    create_info.pEnabledFeatures = &features;

    create_info.enabledExtensionCount = 0;

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
  }

  QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices = {};

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    assert(count < 64);

    VkQueueFamilyProperties properties[64];

    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);

    for (u32 i = 0; i < count; ++i) {
      VkQueueFamilyProperties family = properties[i];

      if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.has_graphics = true;
        indices.graphics = i;
        break;
      }
    }

    return indices;
  }

  bool IsDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = FindQueueFamilies(device);

    return indices.has_graphics;
  }

  bool PickPhysicalDevice() {
    physical_device = VK_NULL_HANDLE;
    u32 device_count = 0;

    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    if (device_count == 0) {
      fprintf(stderr, "Failed to find gpu with Vulkan support\n");
      return false;
    }

    assert(device_count < 32);

    VkPhysicalDevice devices[32];
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

    assert(count <= 12);

    VkLayerProperties properties[12];
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
