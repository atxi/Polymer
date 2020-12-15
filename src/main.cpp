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

#include "miniz.h"

#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <Windows.h>

#define RENDER_ONLY 0

#include "math.h"

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

struct ChunkVertex {
  Vector3f position;
};

void PushVertex(MemoryArena* arena, ChunkVertex* vertices, u32* count, const Vector3f& position) {
  arena->Allocate(sizeof(ChunkVertex), 1);

  vertices[*count].position = position;

  ++*count;
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

#if 0
  if (!game->LoadBlocks()) {
    fprintf(stderr, "Failed to load minecraft assets. Requires blocks.json and 1.16.4.jar.\n");
    return 1;
  }
#endif

#if !RENDER_ONLY
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
#else
  connection->connected = true;
#endif

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

  MSG msg = {};
  float total_time = 0.0f;
  float average_frame_time = 0.0f;
  float last_display_time = 0.0f;

  using ms_float = std::chrono::duration<float, std::milli>;

#if RENDER_ONLY
  Chunk chunk;

  // Create a simple chunk for test generation
  memset(chunk.blocks, 0, sizeof(chunk.blocks));
  for (size_t z = 0; z < 16; ++z) {
    for (size_t x = 0; x < 16; ++x) {
      chunk.blocks[0][z][x] = 1;
    }
  }

  // Chunk generation requires a border around the chunk for accessing neighbors
  // Assume air for now but populate from chunk neighbors later
  u32 bordered_chunk[18 * 18 * 18];
  memset(bordered_chunk, 0, sizeof(bordered_chunk));

  for (s64 y = 0; y < 16; ++y) {
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (y + 1) * 18 * 18 + (z + 1) * 18 + (x + 1);
        bordered_chunk[index] = chunk.blocks[y][z][x];
      }
    }
  }

  ChunkVertex* vertices = (ChunkVertex*)trans_arena.Allocate(0);
  u32 vertex_count = 0;
  for (size_t y = 0; y < 16; ++y) {
    for (size_t z = 0; z < 16; ++z) {
      for (size_t x = 0; x < 16; ++x) {
        size_t index = (y + 1) * 18 * 18 + (z + 1) * 18 + (x + 1);

        u32 bid = bordered_chunk[index];

        if (bid == 0) {
          continue;
        }

        size_t above_index = (y + 2) * 18 * 18 + (z + 1) * 18 + (x + 1);
        size_t below_index = (y)*18 * 18 + (z + 1) * 18 + (x + 1);
        size_t north_index = (y + 1) * 18 * 18 + (z)*18 + (x + 1);
        size_t south_index = (y + 1) * 18 * 18 + (z + 2) * 18 + (x + 1);
        size_t east_index = (y + 1) * 18 * 18 + (z + 1) * 18 + (x + 2);
        size_t west_index = (y + 1) * 18 * 18 + (z + 1) * 18 + (x);

        u32 above_id = bordered_chunk[above_index];
        u32 below_id = bordered_chunk[above_index];
        u32 north_id = bordered_chunk[above_index];
        u32 south_id = bordered_chunk[above_index];
        u32 east_id = bordered_chunk[above_index];
        u32 west_id = bordered_chunk[above_index];

        // TODO: Check actual block model for occlusion, just use air for now
        if (above_id == 0) {
          // Render the top face because this block is visible from above
          // TODO: Get block model elements and render those instead of full block

          PushVertex(&trans_arena, vertices, &vertex_count, Vector3f((float)x, (float)y + 1, (float)z));
          PushVertex(&trans_arena, vertices, &vertex_count, Vector3f((float)x + 1, (float)y + 1, (float)z + 1));
          PushVertex(&trans_arena, vertices, &vertex_count, Vector3f((float)x + 1, (float)y + 1, (float)z));
        }
      }
    }
  }

  RenderMesh mesh = vk_render.AllocateMesh((u8*)vertices, sizeof(ChunkVertex) * vertex_count, vertex_count);

#endif

  while (connection->connected) {
    auto start = std::chrono::high_resolution_clock::now();

    trans_arena.Reset();

#if !RENDER_ONLY
    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }
#endif
    if (vk_render.BeginFrame()) {
      // Render chunk

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

    float frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    total_time += frame_time;
    average_frame_time = average_frame_time * 0.9f + frame_time * 0.1f;

    if (total_time - last_display_time > 10000.0f) {
      printf("%f\n", 1000.0f / average_frame_time);
      fflush(stdout);
      last_display_time = total_time;
    }
  }

#if RENDER_ONLY
  vk_render.FreeMesh(&mesh);
#endif

  vk_render.Cleanup();

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  return polymer::run();
}
