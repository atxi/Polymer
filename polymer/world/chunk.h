#ifndef POLYMER_CHUNK_H_
#define POLYMER_CHUNK_H_

#include <bitset>
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
  }

  inline void Dequeue(s32 chunk_x, s32 chunk_z) {
    for (size_t i = 0; i < count; ++i) {
      if (data[i].x == chunk_x && data[i].z == chunk_z) {
        data[i] = data[--count];
        return;
      }
    }
  }

  inline void Clear() {
    count = 0;
    dirty = false;
  }
};

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

} // namespace world
} // namespace polymer

#endif
