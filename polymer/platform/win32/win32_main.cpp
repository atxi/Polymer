// This needs to be defined at the top of this file because the Windows headers are a mess and break if Windows.h is
// included before it.
#include <ShlObj.h>

// Get rid of the disgusting defines from Windows headers.
#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

#include <polymer/gamestate.h>
#include <polymer/platform/args.h>
#include <polymer/polymer.h>
#include <polymer/types.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Imm32.lib")

#define VOLK_IMPLEMENTATION
#include <volk.h>

namespace polymer {

static Polymer* g_application;
static InputState g_input = {};
static bool g_display_cursor = false;

inline void ToggleCursor() {
  g_display_cursor = !g_display_cursor;
  ShowCursor(g_display_cursor);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  GameState* game = g_application->game;

  switch (msg) {
  case WM_SIZE: {
    g_application->renderer.invalid_swapchain = true;
  } break;
  case WM_CLOSE: {
    DestroyWindow(hwnd);
  } break;
  case WM_DESTROY: {
    PostQuitMessage(0);
  } break;
  case WM_IME_CHAR:
  case WM_CHAR: {
    if (game->chat_window.display_full) {
      game->chat_window.OnInput((u32)wParam);
    }
  } break;
  case WM_KEYDOWN: {
    // This converts the IME input into the original VK code so input can be processed while chat is closed.
    if (wParam == VK_PROCESSKEY) {
      wParam = (WPARAM)ImmGetVirtualKey(hwnd);
    }

    if (wParam == VK_ESCAPE) {
      ToggleCursor();

      game->chat_window.ToggleDisplay();
      memset(&g_input, 0, sizeof(g_input));
    }

    if ((wParam == 'T' || wParam == VK_OEM_2) && !game->chat_window.display_full) {
      ToggleCursor();

      game->chat_window.ToggleDisplay();
      memset(&g_input, 0, sizeof(g_input));

      if (wParam == VK_OEM_2) {
        game->chat_window.input.active = true;
        game->chat_window.OnInput('/');
      }
    }

    if (game->chat_window.display_full) {
      if (wParam == VK_RETURN) {
        ToggleCursor();

        game->chat_window.SendInput(game->connection);
        game->chat_window.ToggleDisplay();
      } else if (wParam == VK_LEFT) {
        game->chat_window.MoveCursor(ui::ChatMoveDirection::Left);
      } else if (wParam == VK_RIGHT) {
        game->chat_window.MoveCursor(ui::ChatMoveDirection::Right);
      } else if (wParam == VK_HOME) {
        game->chat_window.MoveCursor(ui::ChatMoveDirection::Home);
      } else if (wParam == VK_END) {
        game->chat_window.MoveCursor(ui::ChatMoveDirection::End);
      } else if (wParam == VK_DELETE) {
        game->chat_window.OnDelete();
      }
      break;
    }

    if (wParam == 'W') {
      g_input.forward = true;
    } else if (wParam == 'S') {
      g_input.backward = true;
    } else if (wParam == 'A') {
      g_input.left = true;
    } else if (wParam == 'D') {
      g_input.right = true;
    } else if (wParam == VK_SPACE) {
      g_input.climb = true;
    } else if (wParam == VK_SHIFT) {
      g_input.fall = true;
    } else if (wParam == VK_CONTROL) {
      g_input.sprint = true;
    } else if (wParam == VK_TAB) {
      g_input.display_players = true;
    }
  } break;
  case WM_KEYUP: {
    if (wParam == 'W') {
      g_input.forward = false;
    } else if (wParam == 'S') {
      g_input.backward = false;
    } else if (wParam == 'A') {
      g_input.left = false;
    } else if (wParam == 'D') {
      g_input.right = false;
    } else if (wParam == VK_SPACE) {
      g_input.climb = false;
    } else if (wParam == VK_SHIFT) {
      g_input.fall = false;
    } else if (wParam == VK_CONTROL) {
      g_input.sprint = false;
    } else if (wParam == VK_TAB) {
      g_input.display_players = false;
    }
  } break;
  case WM_INPUT: {
    u32 size = 0;

    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));

    RAWINPUT* raw = (RAWINPUT*)g_application->trans_arena.Allocate(size);

    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) != size) {
      fprintf(stderr, "Failed to read raw input data.\n");
      break;
    }

    if (raw->header.dwType == RIM_TYPEMOUSE) {
      s32 x = raw->data.mouse.lLastX;
      s32 y = raw->data.mouse.lLastY;

      if (!g_display_cursor) {
        game->OnWindowMouseMove(x, y);

        RECT rect;
        GetClientRect(hwnd, &rect);
        POINT point = {(rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2};

        ClientToScreen(hwnd, &point);
        SetCursorPos(point.x, point.y);
      }
    }
  } break;
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  return 0;
}

Platform g_Platform;

static const char* Win32GetPlatformName() {
  return "Windows";
}

static PolymerWindow Win32WindowCreate(int width, int height) {
  WNDCLASSEX wc = {};

  wc.cbSize = sizeof(wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"polymer";
  wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);

  if (!RegisterClassEx(&wc)) {
    fprintf(stderr, "Failed to register window.\n");
    return nullptr;
  }

  RECT rect = {0, 0, width, height};
  DWORD ex_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_OVERLAPPEDWINDOW;
  AdjustWindowRect(&rect, ex_style, FALSE);

  u32 window_width = rect.right - rect.left;
  u32 window_height = rect.bottom - rect.top;

  HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"Polymer", ex_style, CW_USEDEFAULT, CW_USEDEFAULT, window_width,
                             window_height, nullptr, nullptr, wc.hInstance, nullptr);

  if (!hwnd) {
    fprintf(stderr, "Failed to create window.\n");
    return nullptr;
  }

  ShowCursor(FALSE);

  RAWINPUTDEVICE mouse_device = {};

  mouse_device.usUsagePage = 0x01; // Generic
  mouse_device.usUsage = 0x02;     // Mouse

  if (RegisterRawInputDevices(&mouse_device, 1, sizeof(RAWINPUTDEVICE)) == FALSE) {
    fprintf(stderr, "Failed to register raw mouse input.\n");
  }

  return (PolymerWindow)hwnd;
}

static bool Win32WindowCreateSurface(PolymerWindow window, void* surface) {
  HWND hwnd = (HWND)window;

  VkWin32SurfaceCreateInfoKHR surface_info = {};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.hinstance = GetModuleHandle(nullptr);
  surface_info.hwnd = hwnd;

  return vkCreateWin32SurfaceKHR(g_application->renderer.instance, &surface_info, nullptr, (VkSurfaceKHR*)surface) ==
         VK_SUCCESS;
}

static IntRect Win32WindowGetRect(PolymerWindow window) {
  IntRect result;
  RECT rect;
  HWND hwnd = (HWND)window;

  GetClientRect(hwnd, &rect);

  result.left = rect.left;
  result.top = rect.top;
  result.right = rect.right;
  result.bottom = rect.bottom;

  return result;
}

static void Win32WindowPump(PolymerWindow window) {
  MSG msg = {};

  while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
    // Only translate the message into character events when chat is open.
    // This allows the WM_KEYDOWN event to convert from IME code to normal VK code when chat is closed.
    if (g_application->game->chat_window.display_full) {
      TranslateMessage(&msg);
    }

    DispatchMessage(&msg);

    if (msg.message == WM_QUIT) {
      g_application->game->connection.Disconnect();
      break;
    }
  }
}

const char* kRequiredExtensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_utils"};
const char* kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
const char* kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};

#define POLYMER_VALIDATION_LAYERS 0

static ExtensionRequest Win32GetExtensionRequest() {
  ExtensionRequest extension_request;

#ifdef NDEBUG
  constexpr size_t kRequiredExtensionCount = polymer_array_count(kRequiredExtensions) - 1;
#else
  constexpr size_t kRequiredExtensionCount = polymer_array_count(kRequiredExtensions);
#endif

#if POLYMER_VALIDATION_LAYERS
  constexpr size_t kValidationLayerCount = polymer_array_count(kValidationLayers);
#else
  constexpr size_t kValidationLayerCount = 0;
#endif

  extension_request.device_extensions = kDeviceExtensions;
  extension_request.device_extension_count = polymer_array_count(kDeviceExtensions);
  extension_request.extensions = kRequiredExtensions;
  extension_request.extension_count = kRequiredExtensionCount;
  extension_request.validation_layers = kValidationLayers;
  extension_request.validation_layer_count = kValidationLayerCount;

  return extension_request;
}

// TODO: Support unicode in path
static String Win32GetAssetStorePath(MemoryArena& arena) {
  char path[1024] = {};

  if (!SHGetSpecialFolderPathA(NULL, path, CSIDL_APPDATA, FALSE)) {
    return {};
  }

  size_t length = strlen(path);

  if (length == 0) {
    return {};
  }

  const char* kAssetStoreName = "\\Polymer\\";
  size_t name_length = strlen(kAssetStoreName);

  char* path_storage = memory_arena_push_type_count(&arena, char, length + name_length);

  memcpy(path_storage, path, length);
  memcpy(path_storage + length, kAssetStoreName, name_length);

  return String(path_storage, length + name_length);
}

static bool Win32FolderExists(const char* path) {
  DWORD attr = GetFileAttributesA(path);

  return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool Win32CreateFolder(const char* path) {
  return CreateDirectoryA(path, NULL);
}

static u8* Win32Allocate(size_t size) {
  u8* result = (u8*)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  return result;
}

static void Win32Free(u8* ptr) {
  VirtualFree(ptr, 0, MEM_RELEASE);
}

} // namespace polymer

int main(int argc, char* argv[]) {
  using namespace polymer;

  if (volkInitialize() != VK_SUCCESS) {
    fprintf(stderr, "Failed to get Vulkan loader.\n");
    fflush(stderr);
    return 1;
  }

  constexpr size_t kPermanentSize = Gigabytes(1);
  constexpr size_t kTransientSize = Megabytes(64);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  Polymer* polymer = perm_arena.Construct<Polymer>(perm_arena, trans_arena, argc, argv);

  polymer->platform = {Win32GetPlatformName,   Win32WindowCreate, Win32WindowCreateSurface,
                       Win32WindowGetRect,     Win32WindowPump,   Win32GetExtensionRequest,
                       Win32GetAssetStorePath, Win32FolderExists, Win32CreateFolder,
                       Win32Allocate,          Win32Free};

  g_Platform = polymer->platform;
  g_application = polymer;

  return polymer->Run(&g_input);
}
