#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

#include "camera.h"
#include "connection.h"
#include "render.h"
#include "types.h"

namespace polymer {

struct MemoryArena;

struct BlockModel {
  u32 m;
};

struct BlockState {
  u32 id;
  char* name;

  // Probably shouldn't be a pointer since BlockState should never be iterated over in the game
  BlockModel* model;
  float x;
  float y;
  bool uvlock;
};

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
  Chunk chunks[16];
};

constexpr size_t kChunkCacheSize = 24;
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

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;
  VulkanRenderer* renderer;

  Connection connection;
  Camera camera;
  World world;

  size_t block_name_count = 0;
  char block_names[32768][32];

  size_t block_state_count = 0;
  BlockState block_states[32768];

  GameState(VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena);

  bool LoadBlocks();

  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_z);
  void OnChunkUnload(s32 chunk_x, s32 chunk_z);
  void OnPlayerPositionAndLook(const Vector3f& position, float yaw, float pitch);

  void OnWindowMouseMove(s32 dx, s32 dy);

  void BuildChunkMesh(s32 chunk_x, s32 chunk_z);

  void Update();

  void FreeMeshes();
};

} // namespace polymer

#endif
