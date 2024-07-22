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
  u32 queue_bitset : 24;
  u32 padding : 7;
  u32 bitmask;
  s32 x;
  s32 z;

  inline bool IsQueued() const {
    return queue_bitset;
  }

  inline bool IsQueued(s32 chunk_y) const {
    if (chunk_y < 0 || chunk_y >= 24) return false;

    return queue_bitset & (1 << chunk_y);
  }

  inline void SetQueued(s32 chunk_y) {
    if (chunk_y >= 0 && chunk_y < 24) {
      queue_bitset |= (1 << chunk_y);
    }
  }

  inline void SetQueued() {
    constexpr u32 kFullBitset = (1 << 24) - 1;
    queue_bitset = kFullBitset;
  }

  inline void ClearQueued() {
    queue_bitset = 0;
  }
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

struct ChunkBuildQueue {
  constexpr static size_t kQueueSize = kChunkCacheSize * kChunkCacheSize * kChunkColumnCount;

  bool dirty = false;
  u32 count = 0;

  ChunkCoord data[kQueueSize];

  inline void Enqueue(s32 chunk_x, s32 chunk_z) {
    if (count >= kQueueSize) {
      fprintf(stderr, "Failed to enqueue chunk in build queue because it was full.\n");
      return;
    }
    data[count++] = {chunk_x, chunk_z};
    dirty = true;
  }

  inline void Dequeue(s32 chunk_x, s32 chunk_z) {
    for (size_t i = 0; i < count; ++i) {
      if (data[i].x == chunk_x && data[i].z == chunk_z) {
        data[i] = data[--count];
        dirty = true;
        return;
      }
    }
  }

  inline void Clear() {
    count = 0;
    dirty = false;
  }
};

// Use a single bit to determine if a chunk exists in the chunk cache.
struct ChunkOccupySet {
  std::bitset<kChunkCacheSize * kChunkCacheSize> bits;

  inline void SetChunk(s32 chunk_x, s32 chunk_z) {
    bits.set(GetChunkCacheIndex(chunk_z) * kChunkCacheSize + GetChunkCacheIndex(chunk_x));
  }

  inline void ClearChunk(s32 chunk_x, s32 chunk_z) {
    bits.set(GetChunkCacheIndex(chunk_z) * kChunkCacheSize + GetChunkCacheIndex(chunk_x), 0);
  }

  inline bool HasChunk(s32 chunk_x, s32 chunk_z) const {
    return bits.test(GetChunkCacheIndex(chunk_z) * kChunkCacheSize + GetChunkCacheIndex(chunk_x));
  }

  inline void Clear() {
    bits.reset();
  }
};

// This is the connectivity state for each face of a chunk to other faces.
// It is used to determine which chunks need to be rendered.
struct ChunkConnectivitySet {
  using VisitSet = std::bitset<16 * 16 * 16>;

  std::bitset<36> connectivity;

  // Computes the new connectivity set and returns whether or not it changed.
  bool Build(const struct World& world, const Chunk& chunk);

  u8 FloodFill(const struct World& world, const Chunk& chunk, VisitSet& visited, s8 start_x, s8 start_y, s8 start_z);

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
  void Update(MemoryArena& trans_arena, const struct World& world, const Camera& camera);
};

} // namespace world
} // namespace polymer

#endif
