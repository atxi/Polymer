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

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

struct CursorPosition {
  int x = 0;
  int y = 0;
};

CursorPosition g_last_cursor;
bool g_frame_chat_open = false;

namespace render {

bool CreateWindowSurface(PolymerWindow window, VkSurfaceKHR* surface) {
  VkResult result = glfwCreateWindowSurface(vk_render.instance, (GLFWwindow*)window, nullptr, surface);

  return result == VK_SUCCESS;
}

IntRect GetWindowRect(PolymerWindow window) {
  IntRect rect = {};

  glfwGetWindowSize((GLFWwindow*)window, &rect.right, &rect.bottom);

  return rect;
}

} // namespace render

inline void ToggleCursor() {
  GLFWwindow* window = (GLFWwindow*)vk_render.hwnd;
  g_display_cursor = !g_display_cursor;

  int mode = g_display_cursor ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
  glfwSetInputMode(window, GLFW_CURSOR, mode);
}

static void OnWindowResize(GLFWwindow* window, int width, int height) {
  vk_render.invalid_swapchain = true;
}

static void OnCursorPosition(GLFWwindow* window, double xpos, double ypos) {
  if (!g_display_cursor) {
    int dx = xpos - g_last_cursor.x;
    int dy = ypos - g_last_cursor.y;

    g_game->OnWindowMouseMove(dx, dy);

    g_last_cursor.x = xpos;
    g_last_cursor.y = ypos;
  }
}

void OnCharacterPress(GLFWwindow* window, unsigned int codepoint) {
  if (g_frame_chat_open) {
    g_frame_chat_open = false;
    return;
  }

  g_frame_chat_open = false;

  if (g_game->chat_window.display_full) {
    g_game->chat_window.OnInput(codepoint);
  }
}

static void OnKeyDown(GLFWwindow* window, int key, int scancode, int action, int mods) {
  switch (key) {
  case GLFW_KEY_ESCAPE: {
    if (action == GLFW_PRESS) {
      ToggleCursor();
      g_game->chat_window.ToggleDisplay();
      g_frame_chat_open = true;
      memset(&g_input, 0, sizeof(g_input));
    }
  } break;
  }

  if (!g_game->chat_window.display_full) {
    switch (key) {
    case GLFW_KEY_SLASH:
    case GLFW_KEY_T: {
      if (action == GLFW_PRESS && !g_game->chat_window.display_full) {
        ToggleCursor();
        g_game->chat_window.ToggleDisplay();
        g_frame_chat_open = true;
        memset(&g_input, 0, sizeof(g_input));

        if (key == GLFW_KEY_SLASH) {
          g_game->chat_window.input.active = true;
          g_game->chat_window.OnInput('/');
        }
      }
    } break;
    case GLFW_KEY_W: {
      g_input.forward = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_S: {
      g_input.backward = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_A: {
      g_input.left = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_D: {
      g_input.right = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_SPACE: {
      g_input.climb = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: {
      g_input.fall = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: {
      g_input.sprint = action != GLFW_RELEASE;
    } break;
    case GLFW_KEY_TAB: {
      g_input.display_players = action != GLFW_RELEASE;
    } break;
    }
  } else if (action != GLFW_RELEASE) {
    switch (key) {
    case GLFW_KEY_ENTER: {
      fflush(stdout);
      ToggleCursor();
      g_game->chat_window.SendInput(g_game->connection);
      g_game->chat_window.ToggleDisplay();
    } break;
    case GLFW_KEY_LEFT: {
      g_game->chat_window.MoveCursor(ui::ChatMoveDirection::Left);
    } break;
    case GLFW_KEY_RIGHT: {
      g_game->chat_window.MoveCursor(ui::ChatMoveDirection::Right);
    } break;
    case GLFW_KEY_HOME: {
      g_game->chat_window.MoveCursor(ui::ChatMoveDirection::Home);
    } break;
    case GLFW_KEY_END: {
      g_game->chat_window.MoveCursor(ui::ChatMoveDirection::End);
    } break;
    case GLFW_KEY_DELETE: {
      g_game->chat_window.OnDelete();
    } break;
    case GLFW_KEY_BACKSPACE: {
      g_game->chat_window.OnInput(0x08);
    } break;
    }
  }
}

int run(const LaunchArgs& args) {
  constexpr size_t kMirrorBufferSize = 65536 * 32;
  constexpr size_t kPermanentSize = Gigabytes(1);
  constexpr size_t kTransientSize = Megabytes(32);

  if (args.help) {
    PrintUsage();
    return 0;
  }

  u8* perm_memory = (u8*)malloc(kPermanentSize);
  u8* trans_memory = (u8*)malloc(kTransientSize);

  MemoryArena perm_arena(perm_memory, kPermanentSize);
  MemoryArena trans_arena(trans_memory, kTransientSize);

  g_trans_arena = &trans_arena;

  vk_render.perm_arena = &perm_arena;
  vk_render.trans_arena = &trans_arena;

  printf("Polymer\n");
  fflush(stdout);

  GameState* game = perm_arena.Construct<GameState>(&vk_render, &perm_arena, &trans_arena);
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

  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize glfw.\n");
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(kWidth, kHeight, "Polymer", NULL, NULL);
  if (!window) {
    fprintf(stderr, "Failed to create glfw window\n");
    glfwTerminate();
    return 1;
  }

  glfwSetWindowSizeCallback(window, OnWindowResize);
  glfwSetKeyCallback(window, OnKeyDown);
  glfwSetCursorPosCallback(window, OnCursorPosition);
  glfwSetCharCallback(window, OnCharacterPress);

  if (glfwRawMouseMotionSupported()) {
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  }

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  uint32_t count;
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);

  const char* kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  // const char* kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};
  const char* kValidationLayers[] = {};

  render::ExtensionRequest extension_request;

  extension_request.extensions = extensions;
  extension_request.extension_count = count;

  extension_request.device_extension_count = polymer_array_count(kDeviceExtensions);
  extension_request.device_extensions = kDeviceExtensions;

  extension_request.validation_layer_count = polymer_array_count(kValidationLayers);
  extension_request.validation_layers = kValidationLayers;

  if (!vk_render.Initialize(window, extension_request)) {
    fprintf(stderr, "Failed to initialize vulkan.\n");
    return 1;
  }

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

  float average_frame_time = 0.0f;

  using ms_float = std::chrono::duration<float, std::milli>;

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

  const char* kAddress = "127.0.0.1";
  const char* kUsername = "Polymer";

  connection->SendHandshake(kProtocolVersion, args.server.data, args.server.size, args.server_port,
                            ProtocolState::Login);
  connection->SendLoginStart(args.username.data, args.username.size);

  memcpy(game->player_manager.client_name, args.username.data, args.username.size);
  game->player_manager.client_name[args.username.size] = 0;

  fflush(stdout);

  ui::DebugTextSystem debug(game->font_renderer);

  while (connection->connected && !glfwWindowShouldClose(window)) {
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

    glfwPollEvents();

    auto end = std::chrono::high_resolution_clock::now();

    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();
    average_frame_time = average_frame_time * 0.9f + frame_time * 0.1f;
  }

  vkDeviceWaitIdle(vk_render.device);
  game->FreeMeshes();

  game->font_renderer.Shutdown(vk_render.device);
  game->chunk_renderer.Shutdown(vk_render.device);

  vk_render.Shutdown();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}

} // namespace polymer

int main(int argc, char* argv[]) {
  polymer::ArgParser arg_parser = polymer::ArgParser::Parse(argc, argv);
  polymer::LaunchArgs args = polymer::LaunchArgs::Create(arg_parser);

  int result = polymer::run(args);

  fflush(stdout);
  fflush(stderr);

  return result;
}
