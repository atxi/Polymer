#include "polymer.h"

#include <polymer/asset/asset_store.h>
#include <polymer/connection.h>
#include <polymer/gamestate.h>
#include <polymer/packet_interpreter.h>
#include <polymer/protocol.h>
#include <polymer/ui/debug.h>

#include <chrono>

namespace polymer {

constexpr const char* kBlocksName = "blocks-1.21.json";

// Window surface width
constexpr u32 kWidth = 1280;
// Window surface height
constexpr u32 kHeight = 720;

using ms_float = std::chrono::duration<float, std::milli>;

Polymer::Polymer(MemoryArena& perm_arena, MemoryArena& trans_arena, int argc, char** argv)
    : perm_arena(perm_arena), trans_arena(trans_arena) {
  ArgParser arg_parser = ArgParser::Parse(argc, argv);
  args = LaunchArgs::Create(arg_parser);

  renderer.perm_arena = &perm_arena;
  renderer.trans_arena = &trans_arena;
}

int Polymer::Run(InputState* input) {
  constexpr size_t kMirrorBufferSize = 65536 * 32;

  renderer.platform = &platform;

  if (!platform.GetPlatformName) {
    fprintf(stderr, "Polymer cannot run without a platform implementation.\n");
    return 1;
  }

  if (args.help) {
    PrintUsage();
    return 0;
  }

  const char* platform_name = platform.GetPlatformName();
  printf("Polymer: %s\n", platform_name);
  fflush(stdout);

  this->game = perm_arena.Construct<GameState>(&renderer, &perm_arena, &trans_arena);

  NetworkQueue net_queue = {};

  if (!net_queue.Initialize()) {
    return 1;
  }

  game->assets.asset_store = perm_arena.Construct<asset::AssetStore>(platform, perm_arena, trans_arena, net_queue);
  game->assets.asset_store->Initialize();

  // TODO: This should be running during a separate scene so download progress can be rendered.
  while (!net_queue.IsEmpty()) {
    net_queue.Run();
  }

  net_queue.Clear();

  PacketInterpreter interpreter(game);
  Connection* connection = &game->connection;

  connection->interpreter = &interpreter;

  // Allocate mirrored ring buffers so they can always be inflated
  connection->read_buffer.size = kMirrorBufferSize;
  connection->read_buffer.data = AllocateMirroredBuffer(connection->read_buffer.size);
  connection->write_buffer.size = kMirrorBufferSize;
  connection->write_buffer.data = AllocateMirroredBuffer(connection->write_buffer.size);

  assert(connection->read_buffer.data);
  assert(connection->write_buffer.data);

  this->window = platform.WindowCreate(kWidth, kHeight);

  render::RenderConfig render_config = {};

  render_config.desired_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  // Enable this for vsync
  // render_config.desired_present_mode = VK_PRESENT_MODE_FIFO_KHR;

  renderer.Initialize(window, render_config);

  {
    auto start = std::chrono::high_resolution_clock::now();

    char* client_path = game->assets.asset_store->GetClientPath(trans_arena);
    if (!game->assets.Load(renderer, client_path, kBlocksName, &game->block_registry)) {
      fprintf(stderr, "Failed to load minecraft assets. Requires %s and %s.\n", kBlocksName, client_path);
      return 1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto frame_time = std::chrono::duration_cast<ms_float>(end - start).count();

    printf("Asset time: %f\n", frame_time);
    fflush(stdout);

    game->chunk_renderer.block_textures = game->assets.block_assets->block_textures;
    game->font_renderer.glyph_page_texture = game->assets.glyph_page_texture;
    game->font_renderer.glyph_size_table = game->assets.glyph_size_table;

    game->world.block_mesher.mapping.Initialize(game->block_registry);
  }

  game->chunk_renderer.CreateLayoutSet(renderer, renderer.device);
  game->font_renderer.CreateLayoutSet(renderer, renderer.device);
  renderer.RecreateSwapchain();

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

  outbound::handshake::SendHandshake(*connection, kProtocolVersion, args.server.data, args.server.size,
                                     args.server_port, ProtocolState::Login);

  outbound::login::SendLoginStart(*connection, args.username.data, args.username.size);

  memcpy(game->player_manager.client_name, args.username.data, args.username.size);
  game->player_manager.client_name[args.username.size] = 0;

  fflush(stdout);

  ui::DebugTextSystem debug(game->font_renderer);

  float average_frame_time = 0.0f;
  float frame_time = 0.0f;

  while (connection->connected) {
    auto start = std::chrono::high_resolution_clock::now();

    trans_arena.Reset();

    Connection::TickResult result = connection->Tick();

    if (result == Connection::TickResult::ConnectionClosed) {
      fprintf(stderr, "Connection closed by server.\n");
    }

    if (renderer.BeginFrame()) {
      game->font_renderer.BeginFrame(renderer.current_frame);

      game->Update(frame_time / 1000.0f, input);

      debug.position = Vector2f(8, 8);
      debug.color = Vector4f(1.0f, 0.67f, 0.0f, 1.0f);

      debug.Write("Polymer [%s]", game->player_manager.client_name);

      debug.color = Vector4f(1, 1, 1, 1);

      debug.Write("platform: %s", platform_name);
      debug.Write("dimension: %.*s", (u32)game->dimension.name.size, game->dimension.name.data);

      int fps = (average_frame_time > 0.0f) ? (u32)(1000.0f / average_frame_time) : 0;
      debug.Write("fps: %d", fps);
      debug.Write("(%.02f, %.02f, %.02f)", game->camera.position.x, game->camera.position.y, game->camera.position.z);

      debug.Write("world tick: %u", game->world.world_tick);

#if DISPLAY_PERF_STATS
      debug.Write("chunks rendered: %u", game->chunk_renderer.stats.chunk_render_count);

      for (size_t i = 0; i < polymer::render::kRenderLayerCount; ++i) {
        const char* name = polymer::render::kRenderLayerNames[i];

        debug.Write("%s vertices rendered: %llu", name, game->chunk_renderer.stats.vertex_counts[i]);
      }
#endif

      game->font_renderer.Draw(game->command_buffers[renderer.current_frame], renderer.current_frame);
      game->SubmitFrame();
      renderer.Render();
    }

    platform.WindowPump(window);

    auto end = std::chrono::high_resolution_clock::now();

    frame_time = std::chrono::duration_cast<ms_float>(end - start).count();
    average_frame_time = average_frame_time * 0.9f + frame_time * 0.1f;
  }

  vkDeviceWaitIdle(renderer.device);
  game->world.FreeMeshes();

  game->font_renderer.Shutdown(renderer.device);
  game->chunk_renderer.Shutdown(renderer.device);

  renderer.Shutdown();

  return 0;
}

} // namespace polymer
