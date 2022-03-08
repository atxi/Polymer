#ifndef POLYMER_WORLD_H_
#define POLYMER_WORLD_H_

#include "types.h"

#include "render/render.h"

namespace polymer {

constexpr size_t kChunkColumnCount = 24;

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
  Chunk chunks[kChunkColumnCount];
};

constexpr size_t kChunkCacheSize = 32;
struct World {
  // Store the chunk data separately to make render iteration faster
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];
  ChunkSectionInfo chunk_infos[kChunkCacheSize][kChunkCacheSize];
  render::RenderMesh meshes[kChunkCacheSize][kChunkCacheSize][kChunkColumnCount];

  inline u32 GetChunkCacheIndex(s32 v) {
    return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
  }
};

} // namespace polymer

#endif
