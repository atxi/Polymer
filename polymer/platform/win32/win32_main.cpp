// #define POLYMER_CURL_TEST

#ifdef POLYMER_CURL_TEST
//
#include <curl/curl.h>
//
#endif

#include <polymer/asset/asset_system.h>
#include <polymer/connection.h>
#include <polymer/gamestate.h>
#include <polymer/memory.h>
#include <polymer/packet_interpreter.h>
#include <polymer/platform/args.h>
#include <polymer/protocol.h>
#include <polymer/types.h>

#include <polymer/render/render.h>
#include <polymer/ui/debug.h>

#include <chrono>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <polymer/miniz.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Imm32.lib")

namespace polymer {

constexpr const char* kMinecraftJar = "1.20.1.jar";
constexpr const char* kBlocksName = "blocks-1.20.1.json";

// Window surface width
constexpr u32 kWidth = 1280;
// Window surface height
constexpr u32 kHeight = 720;

render::VulkanRenderer vk_render;

static GameState* g_game = nullptr;
static MemoryArena* g_trans_arena = nullptr;
static InputState g_input = {};

static bool g_display_cursor = false;

namespace render {

const char* const kRequiredExtensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_utils"};
const char* const kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
const char* const kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
constexpr size_t kRequiredExtensionCount = polymer_array_count(kRequiredExtensions) - 1;
#else
constexpr bool kEnableValidationLayers = true;
constexpr size_t kRequiredExtensionCount = polymer_array_count(kRequiredExtensions);
#endif

bool CreateWindowSurface(PolymerWindow window, VkSurfaceKHR* surface) {
  HWND hwnd = (HWND)window;

  VkWin32SurfaceCreateInfoKHR surface_info = {};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.hinstance = GetModuleHandle(nullptr);
  surface_info.hwnd = hwnd;

  return vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, surface) == VK_SUCCESS;
}

IntRect GetWindowRect(PolymerWindow window) {
  IntRect result;
  RECT rect;

  GetClientRect(hwnd, &rect);

  result.left = rect.left;
  result.top = rect.top;
  result.right = rect.right;
  result.bottom = rect.bottom;

  return result;
}

} // namespace render

inline void ToggleCursor() {
  g_display_cursor = !g_display_cursor;
  ShowCursor(g_display_cursor);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_SIZE: {
    vk_render.invalid_swapchain = true;
  } break;
  case WM_CLOSE: {
    DestroyWindow(hwnd);
  } break;
  case WM_DESTROY: {
    PostQuitMessage(0);
  } break;
  case WM_IME_CHAR:
  case WM_CHAR: {
    if (g_game->chat_window.display_full) {
      g_game->chat_window.OnInput((u32)wParam);
    }
  } break;
  case WM_KEYDOWN: {
    // This converts the IME input into the original VK code so input can be processed while chat is closed.
    if (wParam == VK_PROCESSKEY) {
      wParam = (WPARAM)ImmGetVirtualKey(hwnd);
    }

    if (wParam == VK_ESCAPE) {
      ToggleCursor();

      g_game->chat_window.ToggleDisplay();
      memset(&g_input, 0, sizeof(g_input));
    }

    if ((wParam == 'T' || wParam == VK_OEM_2) && !g_game->chat_window.display_full) {
      ToggleCursor();

      g_game->chat_window.ToggleDisplay();
      memset(&g_input, 0, sizeof(g_input));

      if (wParam == VK_OEM_2) {
        g_game->chat_window.input.active = true;
        g_game->chat_window.OnInput('/');
      }
    }

    if (g_game->chat_window.display_full) {
      if (wParam == VK_RETURN) {
        ToggleCursor();

        g_game->chat_window.SendInput(g_game->connection);
        g_game->chat_window.ToggleDisplay();
      } else if (wParam == VK_LEFT) {
        g_game->chat_window.MoveCursor(ui::ChatMoveDirection::Left);
      } else if (wParam == VK_RIGHT) {
        g_game->chat_window.MoveCursor(ui::ChatMoveDirection::Right);
      } else if (wParam == VK_HOME) {
        g_game->chat_window.MoveCursor(ui::ChatMoveDirection::Home);
      } else if (wParam == VK_END) {
        g_game->chat_window.MoveCursor(ui::ChatMoveDirection::End);
      } else if (wParam == VK_DELETE) {
        g_game->chat_window.OnDelete();
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

    RAWINPUT* raw = (RAWINPUT*)g_trans_arena->Allocate(size);

    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) != size) {
      fprintf(stderr, "Failed to read raw input data.\n");
      break;
    }

    if (raw->header.dwType == RIM_TYPEMOUSE) {
      s32 x = raw->data.mouse.lLastX;
      s32 y = raw->data.mouse.lLastY;

      if (!g_display_cursor) {
        g_game->OnWindowMouseMove(x, y);

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

int run(const LaunchArgs& args) {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = Gigabytes(1);
  constexpr size_t kTransientSize = Megabytes(32);

  if (args.help) {
    PrintUsage();
    return 0;
  }

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  g_trans_arena = &trans_arena;

  vk_render.perm_arena = &perm_arena;
  vk_render.trans_arena = &trans_arena;

  printf("Polymer\n");
  fflush(stdout);

  GameState* game = memory_arena_construct_type(&perm_arena, GameState, &vk_render, &perm_arena, &trans_arena);
  PacketInterpreter interpreter(game);
  Connection* connection = &game->connection;

  g_game = game;
  connection->interpreter = &interpreter;

  // Allocate mirrored ring buffers so they can always be inflated
  connection->read_buffer.size = kMirrorBufferSize;
  connection->read_buffer.data = AllocateMirroredBuffer(connection->read_buffer.size);
  connection->write_buffer.size = kMirrorBufferSize;
  connection->write_buffer.data = AllocateMirroredBuffer(connection->write_buffer.size);

  assert(connection->read_buffer.data);
  assert(connection->write_buffer.data);

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
    return 1;
  }

  RECT rect = {0, 0, kWidth, kHeight};
  DWORD ex_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_OVERLAPPEDWINDOW;
  AdjustWindowRect(&rect, ex_style, FALSE);

  u32 window_width = rect.right - rect.left;
  u32 window_height = rect.bottom - rect.top;

  HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"Polymer", ex_style, CW_USEDEFAULT, CW_USEDEFAULT, window_width,
                             window_height, nullptr, nullptr, wc.hInstance, nullptr);

  if (!hwnd) {
    fprintf(stderr, "Failed to create window.\n");
    return 1;
  }

  vk_render.Initialize(hwnd);

  {

    using ms_float = std::chrono::duration<float, std::milli>;
    auto start = std::chrono::high_resolution_clock::now();

    if (!g_game->assets.Load(vk_render, kMinecraftJar, kBlocksName, &game->block_registry)) {
      fprintf(stderr, "Failed to load minecraft assets. Requires %s and %s.\n", kBlocksName, kMinecraftJar);
      return 1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    printf("Asset time: %f\n", frame_time);
    fflush(stdout);

    g_game->chunk_renderer.block_textures = g_game->assets.block_assets->block_textures;
    g_game->font_renderer.glyph_page_texture = g_game->assets.glyph_page_texture;
    g_game->font_renderer.glyph_size_table = g_game->assets.glyph_size_table;

    g_game->block_mesher.mapping.Initialize(g_game->block_registry);
  }

  MSG msg = {};
  float average_frame_time = 0.0f;

  using ms_float = std::chrono::duration<float, std::milli>;

  ShowCursor(FALSE);

  RAWINPUTDEVICE mouse_device = {};

  mouse_device.usUsagePage = 0x01; // Generic
  mouse_device.usUsage = 0x02;     // Mouse

  if (RegisterRawInputDevices(&mouse_device, 1, sizeof(RAWINPUTDEVICE)) == FALSE) {
    fprintf(stderr, "Failed to register raw mouse input.\n");
  }

  float frame_time = 0.0f;

  g_game->chunk_renderer.CreateLayoutSet(vk_render, vk_render.device);
  g_game->font_renderer.CreateLayoutSet(vk_render, vk_render.device);
  vk_render.RecreateSwapchain();

  printf("Connecting to '%.*s:%hu' with username '%.*s'.\n", (u32)args.server.size, args.server.data, args.server_port,
         (u32)args.username.size, args.username.data);
  fflush(stdout);

  ConnectResult connect_result = connection->Connect(args.server.data, args.server_port);

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

  connection->SendHandshake(kProtocolVersion, args.server, args.server_port, ProtocolState::Login);
  connection->SendLoginStart(args.username);

  memcpy(game->player_manager.client_name, args.username.data, args.username.size);
  game->player_manager.client_name[args.username.size] = 0;

  fflush(stdout);

  ui::DebugTextSystem debug(game->font_renderer);

  while (connection->connected) {
    auto start = std::chrono::high_resolution_clock::now();

    trans_arena.Reset();

    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }

    if (vk_render.BeginFrame()) {
      game->font_renderer.BeginFrame(vk_render.current_frame);

      game->Update(frame_time / 1000.0f, &g_input);

      debug.position = Vector2f(8, 8);
      debug.color = Vector4f(1.0f, 0.67f, 0.0f, 1.0f);

      debug.Write("Polymer [%s]", game->player_manager.client_name);

      debug.color = Vector4f(1, 1, 1, 1);

      int fps = (average_frame_time > 0.0f) ? (u32)(1000.0f / average_frame_time) : 0;
      debug.Write("fps: %d", fps);
      debug.Write("(%.02f, %.02f, %.02f)", g_game->camera.position.x, g_game->camera.position.y,
                  g_game->camera.position.z);

      debug.Write("world tick: %u", game->world_tick);

#if DISPLAY_PERF_STATS
      debug.Write("chunks rendered: %u", g_game->chunk_renderer.stats.chunk_render_count);

      for (size_t i = 0; i < polymer::render::kRenderLayerCount; ++i) {
        const char* name = polymer::render::kRenderLayerNames[i];

        debug.Write("%s vertices rendered: %llu", name, g_game->chunk_renderer.stats.vertex_counts[i]);
      }
#endif

      game->font_renderer.Draw(game->command_buffers[vk_render.current_frame], vk_render.current_frame);
      game->SubmitFrame();
      vk_render.Render();
    }

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      // Only translate the message into character events when chat is open.
      // This allows the WM_KEYDOWN event to convert from IME code to normal VK code when chat is closed.
      if (g_game->chat_window.display_full) {
        TranslateMessage(&msg);
      }

      DispatchMessage(&msg);

      if (msg.message == WM_QUIT) {
        connection->Disconnect();
        break;
      }
    }

    auto end = std::chrono::high_resolution_clock::now();

    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();
    average_frame_time = average_frame_time * 0.9f + frame_time * 0.1f;
  }

  vkDeviceWaitIdle(vk_render.device);
  game->FreeMeshes();

  game->font_renderer.Shutdown(vk_render.device);
  game->chunk_renderer.Shutdown(vk_render.device);

  vk_render.Shutdown();

  return 0;
}

} // namespace polymer

#ifdef POLYMER_CURL_TEST

static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userp) {
  size_t full_size = size * nmemb;

  polymer::String* out = (polymer::String*)userp;

  memcpy(out->data + out->size, ptr, full_size);
  out->size += full_size;

  return full_size;
}

bool curl_test() {
  const char* kRequestUrl =
      "https://piston-meta.mojang.com/v1/packages/9d58fdd2538c6877fb5c5c558ebc60ee0b6d0e84/5.json";

  CURL* curl;
  CURLcode res;

  polymer::String response;

  response.data = (char*)malloc(polymer::Megabytes(32));

  res = curl_global_init(CURL_GLOBAL_DEFAULT);

  if (res != CURLE_OK) {
    fprintf(stderr, "curl_global_init() failed: %s\n", curl_easy_strerror(res));
    return false;
  }

  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, kRequestUrl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
  }

  printf("%.*s\n", (int)response.size, response.data);
  free(response.data);

  curl_global_cleanup();
  return 1;
}

#endif

int main(int argc, char* argv[]) {
#ifdef POLYMER_CURL_TEST
  curl_test();
#else
  polymer::ArgParser arg_parser = polymer::ArgParser::Parse(argc, argv);
  polymer::LaunchArgs args = polymer::LaunchArgs::Create(arg_parser);

  int result = polymer::run(args);
#endif
  fflush(stdout);
  fflush(stderr);

  return 0;
}
