#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

#include "block.h"
#include "camera.h"
#include "connection.h"
#include "render.h"
#include "types.h"

namespace polymer {

struct MemoryArena;

struct ChunkCoord {
  s32 x;
  s32 z;
};

struct Chunk {
  u32 blocks[16][16][16];
};

struct ChunkSectionInfo {
  s32 x;
  s32 z;
  u32 bitmask;
  bool loaded;
};

struct ChunkSection {
  ChunkSectionInfo* info;
  Chunk chunks[16];
};

constexpr size_t kChunkCacheSize = 32;
struct World {
  // Store the chunk data separately to make render iteration faster
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];
  ChunkSectionInfo chunk_infos[kChunkCacheSize][kChunkCacheSize];
  RenderMesh meshes[kChunkCacheSize][kChunkCacheSize][16];

  size_t build_queue_count;
  ChunkCoord build_queue[1024];

  inline u32 GetChunkCacheIndex(s32 v) {
    return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
  }
};

struct ChunkBuildContext {
  s32 chunk_x;
  s32 chunk_z;

  u32 x_index = 0;
  u32 z_index = 0;

  ChunkSection* section = nullptr;
  ChunkSection* east_section = nullptr;
  ChunkSection* west_section = nullptr;
  ChunkSection* north_section = nullptr;
  ChunkSection* south_section = nullptr;

  ChunkBuildContext(s32 chunk_x, s32 chunk_z) : chunk_x(chunk_x), chunk_z(chunk_z) {}

  bool IsBuildable() {
    return east_section->info->loaded && west_section->info->loaded && north_section->info->loaded &&
           south_section->info->loaded;
  }

  bool GetNeighbors(World* world) {
    x_index = world->GetChunkCacheIndex(chunk_x);
    z_index = world->GetChunkCacheIndex(chunk_z);

    u32 xeast_index = world->GetChunkCacheIndex(chunk_x + 1);
    u32 xwest_index = world->GetChunkCacheIndex(chunk_x - 1);
    u32 znorth_index = world->GetChunkCacheIndex(chunk_z - 1);
    u32 zsouth_index = world->GetChunkCacheIndex(chunk_z + 1);

    section = &world->chunks[z_index][x_index];
    east_section = &world->chunks[z_index][xeast_index];
    west_section = &world->chunks[z_index][xwest_index];
    north_section = &world->chunks[znorth_index][x_index];
    south_section = &world->chunks[zsouth_index][x_index];

    return IsBuildable();
  }
};

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

struct BlockRegistry {
  size_t state_count;
  BlockState* states;

  size_t info_count;
  BlockStateInfo* infos;
};

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;
  VulkanRenderer* renderer;

  Connection connection;
  Camera camera;
  World world;

  BlockRegistry block_registry;

  GameState(VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena);

  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_z);
  void OnChunkUnload(s32 chunk_x, s32 chunk_z);
  void OnPlayerPositionAndLook(const Vector3f& position, float yaw, float pitch);
  void OnDimensionChange();

  void OnWindowMouseMove(s32 dx, s32 dy);

  void BuildChunkMesh(ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_z);
  void BuildChunkMesh(ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_y, s32 chunk_z);

  void Update(float dt, InputState* input);

  void FreeMeshes();
};

} // namespace polymer

#endif
