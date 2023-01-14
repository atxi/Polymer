#ifndef POLYMER_WORLD_H_
#define POLYMER_WORLD_H_

#include <polymer/render/chunk_renderer.h>
#include <polymer/types.h>

namespace polymer {
namespace world {

constexpr size_t kChunkColumnCount = 24;

struct ChunkCoord {
  s32 x;
  s32 z;
};

struct Chunk {
  u32 blocks[16][16][16];

  // The bottom 4 bits contain the skylight data and the upper 4 bits contain the block
  u8 lightmap[16][16][16];
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
  render::RenderMesh meshes[render::kRenderLayerCount];
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

} // namespace world
} // namespace polymer

#endif
