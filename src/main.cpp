#include "asset/asset_system.h"
#include "connection.h"
#include "gamestate.h"
#include "memory.h"
#include "packet_interpreter.h"
#include "types.h"

#include "render/render.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "miniz.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

namespace polymer {

constexpr const char* kServerIp = "127.0.0.1";
constexpr u16 kServerPort = 25565;
constexpr const char* kMinecraftJar = "1.19.jar";

// Window surface width
constexpr u32 kWidth = 1280;
// Window surface height
constexpr u32 kHeight = 720;

render::VulkanRenderer vk_render;

static GameState* g_game = nullptr;
static MemoryArena* g_trans_arena = nullptr;
static InputState g_input = {};

static bool g_display_cursor = false;

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
  case WM_KEYDOWN: {
    if (wParam == 'T' || wParam == VK_ESCAPE) {
      g_display_cursor = !g_display_cursor;

      ShowCursor(g_display_cursor);
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

int run() {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = Gigabytes(1);
  constexpr size_t kTransientSize = Megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  g_trans_arena = &trans_arena;

  vk_render.perm_arena = &perm_arena;
  vk_render.trans_arena = &trans_arena;

  printf("Polymer\n");

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
    exit(1);
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
    exit(1);
  }

  vk_render.Initialize(hwnd);

  {

    using ms_float = std::chrono::duration<float, std::milli>;
    auto start = std::chrono::high_resolution_clock::now();

    if (!g_game->assets.Load(vk_render, kMinecraftJar, "blocks.json")) {
      fprintf(stderr, "Failed to load minecraft assets. Requires blocks.json and %s.\n", kMinecraftJar);
      return 1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    printf("Asset time: %f\n", frame_time);
    fflush(stdout);

    vk_render.chunk_renderer.block_textures = g_game->assets.block_assets->block_textures;
    vk_render.font_renderer.glyph_page_texture = g_game->assets.glyph_page_texture;
    vk_render.font_renderer.glyph_size_table = g_game->assets.glyph_size_table;
    game->block_registry = g_game->assets.block_assets->block_registry;
  }

  MSG msg = {};
  float total_time = 0.0f;
  float average_frame_time = 0.0f;
  float last_display_time = 0.0f;

  using ms_float = std::chrono::duration<float, std::milli>;

  ShowCursor(FALSE);

  RAWINPUTDEVICE mouse_device = {};

  mouse_device.usUsagePage = 0x01; // Generic
  mouse_device.usUsage = 0x02;     // Mouse

  if (RegisterRawInputDevices(&mouse_device, 1, sizeof(RAWINPUTDEVICE)) == FALSE) {
    fprintf(stderr, "Failed to register raw mouse input.\n");
  }

  float frame_time = 0.0f;

  vk_render.chunk_renderer.CreateLayoutSet(vk_render, vk_render.device);
  vk_render.font_renderer.CreateLayoutSet(vk_render, vk_render.device);
  vk_render.RecreateSwapchain();

  ConnectResult connect_result = connection->Connect(kServerIp, kServerPort);

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

  constexpr u32 kProtocolVersion = 760;

  connection->SendHandshake(kProtocolVersion, kServerIp, kServerPort, ProtocolState::Login);
  connection->SendLoginStart("polymer");

  fflush(stdout);

  while (connection->connected) {
    auto start = std::chrono::high_resolution_clock::now();

    trans_arena.Reset();

    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }

    if (vk_render.BeginFrame()) {
      game->Update(frame_time / 1000.0f, &g_input);

      using namespace polymer::render;

      FontStyleFlags style = FontStyle_Background | FontStyle_DropShadow;

      float y = 8;

      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), POLY_STR("Polymer"), style,
                                         Vector4f(1.0f, 0.67f, 0.0f, 1.0f));
      y += 16;

      char text[256] = {};
      int fps = (average_frame_time > 0.0f) ? (u32)(1000.0f / average_frame_time) : 0;
      sprintf(text, "%d fps", fps);

      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), String(text), style);
      y += 16;

      sprintf(text, "(%.02f, %.02f, %.02f)", g_game->camera.position.x, g_game->camera.position.y,
              g_game->camera.position.z);

      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), String(text), style);
      y += 16;

      sprintf(text, "%d chunks rendered", g_game->chunk_render_count);
      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), String(text), style);
      y += 16;

      sprintf(text, "%llu opaque vertices rendered", g_game->opaque_vertex_count);
      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), String(text), style);
      y += 16;

      sprintf(text, "%llu flora vertices rendered", g_game->flora_vertex_count);
      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), String(text), style);
      y += 16;

      sprintf(text, "%llu alpha vertices rendered", g_game->alpha_vertex_count);
      vk_render.font_renderer.RenderText(Vector3f(8, y, 0), String(text), style);
      y += 16;

      vk_render.Render();
    }

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);

      if (msg.message == WM_QUIT) {
        connection->Disconnect();
        break;
      }
    }

    auto end = std::chrono::high_resolution_clock::now();

    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    total_time += frame_time;
    average_frame_time = average_frame_time * 0.9f + frame_time * 0.1f;

    if (total_time - last_display_time > 10000.0f) {
      printf("%f\n", 1000.0f / average_frame_time);
      fflush(stdout);
      last_display_time = total_time;
    }
  }

  vkDeviceWaitIdle(vk_render.device);
  game->FreeMeshes();

  vk_render.Cleanup();

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
