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
  bool loaded;
  u32 bitmask;
  s32 x;
  s32 z;
};

struct ChunkSection {
  ChunkSectionInfo* info;
  Chunk chunks[kChunkColumnCount];
};

struct ChunkMesh {
  render::RenderMesh meshes[kRenderLayerCount];
};

constexpr size_t kChunkCacheSize = 32;
struct World {
  // Store the chunk data separately to make render iteration faster
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];
  ChunkSectionInfo chunk_infos[kChunkCacheSize][kChunkCacheSize];
  ChunkMesh meshes[kChunkCacheSize][kChunkCacheSize][kChunkColumnCount];

  inline u32 GetChunkCacheIndex(s32 v) {
    return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
  }
};

} // namespace polymer

#endif
