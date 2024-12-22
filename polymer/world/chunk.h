#ifndef POLYMER_CHUNK_H_
#define POLYMER_CHUNK_H_

#include <bitset>
#include <polymer/camera.h>
#include <polymer/memory.h>
#include <polymer/render/block_mesher.h>
#include <polymer/render/chunk_renderer.h>
#include <polymer/render/render.h>
#include <polymer/types.h>
#include <stdio.h>

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
  u32 loaded : 1;
  u32 dirty_connectivity_set : 24;
  u32 padding : 7;

  u32 dirty_mesh_set : 24;
  u32 padding_mesh : 8;

  u32 bitmask;
  s32 x;
  s32 z;
};

struct ChunkSection {
  ChunkSectionInfo* info;
  Chunk* chunks[kChunkColumnCount];
};

struct ChunkMesh {
  render::RenderMesh meshes[render::kRenderLayerCount];
};

constexpr size_t kMaxViewDistance = 32;
// We need to be able to wrap around without overwriting any used chunks.
constexpr size_t kChunkCacheSize = kMaxViewDistance * 2 + 4;

inline constexpr u32 GetChunkCacheIndex(s32 v) {
  return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
}

// This is the connectivity state for each face of a chunk to other faces.
// It is used to determine which chunks need to be rendered.
struct ChunkConnectivitySet {
  struct Coord {
    s8 x;
    s8 y;
    s8 z;
    s8 pad;
  };

  using VisitSet = std::bitset<16 * 16 * 16>;

  std::bitset<36> connectivity;

  // Computes the new connectivity set and returns whether or not it changed.
  bool Build(const struct World& world, const Chunk& chunk);

  u8 FloodFill(const struct World& world, const Chunk& chunk, VisitSet& visited, Coord* queue, s8 start_x, s8 start_y,
               s8 start_z);

  inline bool HasFaceConnectivity(BlockFace face) const {
    for (size_t i = 0; i < 6; ++i) {
      size_t index = (size_t)face * 6 + i;

      if (connectivity.test(index)) return true;
    }

    return false;
  }

  inline bool IsConnected(BlockFace from, BlockFace to) const {
    constexpr size_t kFaceCount = 6;

    // We only need to check one index because it should be set for both when connectivity is set.
    size_t index = (size_t)from + ((size_t)to * kFaceCount);

    return connectivity.test(index);
  }

  inline void Clear() {
    connectivity.reset();
  }
};

struct VisibleChunk {
  s32 chunk_x;
  s32 chunk_y;
  s32 chunk_z;
};

struct ChunkConnectivityGraph {
  ChunkConnectivitySet chunk_connectivity[kChunkCacheSize][kChunkCacheSize][kChunkColumnCount];
  VisibleChunk visible_set[kChunkCacheSize * kChunkCacheSize * kChunkColumnCount];
  size_t visible_count = 0;

  // This computes the connectivity of the provided chunk to the neighboring chunks.
  void Build(const struct World& world, const Chunk* chunk, size_t x_index, size_t z_index, s32 chunk_y);

  // Rebuilds the visible set.
  void Update(MemoryArena& trans_arena, struct World& world, const Camera& camera);
};

} // namespace world
} // namespace polymer

#endif
