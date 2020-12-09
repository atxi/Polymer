#include "connection.h"
#include "gamestate.h"
#include "inflate.h"
#include "memory.h"
#include "packet_interpreter.h"
#include "types.h"

#include <cassert>
#include <cstdio>

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

const char* const kRequiredExtensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};

bool CreateWindowSurface(VkInstance instance, HWND hwnd, VkSurfaceKHR* surface) {
  VkWin32SurfaceCreateInfoKHR surface_info = {};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.hinstance = GetModuleHandle(nullptr);
  surface_info.hwnd = hwnd;

  VkResult result = vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, surface);

  return result == VK_SUCCESS;
}

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

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pNext = NULL;
  app_info.pApplicationName = "polymer_instance";
  app_info.applicationVersion = 1;
  app_info.pEngineName = "polymer_instance";
  app_info.engineVersion = 1;
  app_info.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo inst_info = {};
  inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_info.pNext = NULL;
  inst_info.flags = 0;
  inst_info.pApplicationInfo = &app_info;
  inst_info.enabledExtensionCount = polymer_array_count(kRequiredExtensions);
  inst_info.ppEnabledExtensionNames = kRequiredExtensions;
  inst_info.enabledLayerCount = 0;
  inst_info.ppEnabledLayerNames = NULL;

  VkInstance instance;
  VkResult res;

  res = vkCreateInstance(&inst_info, NULL, &instance);
  if (res != VK_SUCCESS) {
    fprintf(stderr, "Error creating Vulkan instance: %d\n", res);
    return 1;
  }

  VkSurfaceKHR surface;

  if (!CreateWindowSurface(instance, hwnd, &surface)) {
    fprintf(stderr, "Failed to create window surface.\n");
    return 1;
  }

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

  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);

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
