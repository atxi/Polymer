#include "connection.h"
#include "gamestate.h"
#include "memory.h"
#include "packet_interpreter.h"
#include "render.h"
#include "types.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

namespace polymer {

// Window surface width
constexpr u32 kWidth = 1280;
// Window surface height
constexpr u32 kHeight = 720;

VulkanRenderer vk_render;

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
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  return 0;
}

int run() {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = gigabytes(1);
  constexpr size_t kTransientSize = megabytes(32);

  u8* perm_memory = (u8*)VirtualAlloc(NULL, kPermanentSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  u8* trans_memory = (u8*)VirtualAlloc(NULL, kTransientSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  vk_render.trans_arena = &trans_arena;

  printf("Polymer\n");

  GameState* game = memory_arena_construct_type(&perm_arena, GameState, &vk_render, &perm_arena, &trans_arena);
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
  float total_time = 0.0f;
  float average_frame_time = 0.0f;
  float last_display_time = 0.0f;

  using ms_float = std::chrono::duration<float, std::milli>;
  while (connection->connected) {
    auto start = std::chrono::high_resolution_clock::now();

    trans_arena.Reset();

    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }

    vk_render.Render();

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);

      if (msg.message == WM_QUIT) {
        connection->Disconnect();
        break;
      }
    }

    auto end = std::chrono::high_resolution_clock::now();

    float frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    total_time += frame_time;
    average_frame_time = average_frame_time * 0.9f + frame_time * 0.1f;

    if (total_time - last_display_time > 10000.0f) {
      printf("%f\n", 1000.0f / average_frame_time);
      fflush(stdout);
      last_display_time = total_time;
    }
  }

  vk_render.Cleanup();

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
