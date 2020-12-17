#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

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

constexpr size_t kChunkCacheSize = 24;

struct Chunk {
  u32 blocks[16][16][16];
  RenderMesh mesh;
};

struct ChunkSection {
  s32 x;
  s32 z;
  bool loaded;
  Chunk chunks[16];

  ChunkSection() : loaded(false) {}
};

inline u32 GetChunkCacheIndex(s32 v) {
  return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
}

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;
  VulkanRenderer* renderer;

  Connection connection;

  size_t block_name_count = 0;
  char block_names[32768][32];

  size_t block_state_count = 0;
  BlockState block_states[32768];

  // Chunk cache
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];

  GameState(VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena);

  bool LoadBlocks();

  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_y, s32 chunk_z);
  void OnChunkUnload(s32 chunk_x, s32 chunk_z);

  void RenderGame();

  void FreeMeshes();
};

} // namespace polymer

#endif
