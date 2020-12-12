#ifndef POLYMER_GAMESTATE_H_
#define POLYMER_GAMESTATE_H_

#include "connection.h"
#include "types.h"

namespace polymer {

struct MemoryArena;

struct BlockState {
  u32 id;
  char* name;
};

constexpr size_t kChunkCacheSize = 48;

struct Chunk {
  u32 blocks[16][16][16];
};

struct ChunkSection {
  s32 x;
  s32 z;

  Chunk chunks[16];
};

inline u32 GetChunkCacheIndex(s32 v) {
  return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
}

struct GameState {
  MemoryArena* perm_arena;
  MemoryArena* trans_arena;

  Connection connection;

  size_t block_name_count = 0;
  char block_names[32768][32];

  size_t block_state_count = 0;
  BlockState block_states[32768];

  // Chunk cache
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];

  GameState(MemoryArena* perm_arena, MemoryArena* trans_arena)
      : perm_arena(perm_arena), trans_arena(trans_arena), connection(*perm_arena) {}

  void LoadBlocks();

  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_y, s32 chunk_z);
};

} // namespace polymer

#endif
