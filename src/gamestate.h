#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

#include "asset/asset_system.h"
#include "camera.h"
#include "connection.h"
#include "render/block_mesher.h"
#include "render/render.h"
#include "types.h"
#include "world/block.h"
#include "world/dimension.h"
#include "world/world.h"

namespace polymer {

struct MemoryArena;

// TODO: Make this more advanced
struct InputState {
  bool forward;
  bool backward;
  bool left;
  bool right;
  bool climb;
  bool fall;
  bool sprint;
  bool display_players;
};

struct Player {
  char name[16];
  char uuid[16];

  u8 ping;
  u8 gamemode;
};

struct PlayerManager {
  Player players[256];
  size_t player_count = 0;

  void AddPlayer(const String& name, const String& uuid, u8 ping, u8 gamemode);
  void RemovePlayer(const String& uuid);
  Player* GetPlayerByUuid(const String& uuid);

  void RenderPlayerList(render::VulkanRenderer& renderer);
};

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;
  render::VulkanRenderer* renderer;
  asset::AssetSystem assets;
  world::DimensionCodec dimension_codec;
  world::DimensionType dimension;

  Connection connection;
  Camera camera;
  world::World world;

  PlayerManager player_manager;

  u32 chunk_render_count;
  u64 opaque_vertex_count;
  u64 flora_vertex_count;
  u64 alpha_vertex_count;

  render::ChunkBuildQueue build_queue;

  world::BlockRegistry block_registry;

  GameState(render::VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena);

  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_z);
  void OnChunkUnload(s32 chunk_x, s32 chunk_z);
  void OnPlayerPositionAndLook(const Vector3f& position, float yaw, float pitch);
  void OnDimensionChange();

  void OnWindowMouseMove(s32 dx, s32 dy);

  void BuildChunkMesh(render::ChunkBuildContext* ctx);
  void BuildChunkMesh(render::ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_y, s32 chunk_z);

  void ImmediateRebuild(render::ChunkBuildContext* ctx, s32 chunk_y);

  void Update(float dt, InputState* input);

  void FreeMeshes();
};

} // namespace polymer

#endif
