#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

#include "asset_system.h"
#include "block.h"
#include "camera.h"
#include "connection.h"
#include "types.h"
#include "world.h"

#include "render/block_mesher.h"
#include "render/render.h"

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
};

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;
  render::VulkanRenderer* renderer;
  AssetSystem assets;

  Connection connection;
  Camera camera;
  World world;

  render::ChunkBuildQueue build_queue;

  BlockRegistry block_registry;

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
