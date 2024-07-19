#ifndef POLYMER_WORLD_H_
#define POLYMER_WORLD_H_

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

struct World {
  MemoryPool<Chunk> chunk_pool;

  // Store the chunk data separately to make render iteration faster
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];
  ChunkSectionInfo chunk_infos[kChunkCacheSize][kChunkCacheSize];
  ChunkMesh meshes[kChunkCacheSize][kChunkCacheSize][kChunkColumnCount];

  MemoryArena& trans_arena;
  render::VulkanRenderer& renderer;

  ChunkBuildQueue build_queue;
  render::BlockMesher block_mesher;
  u32 world_tick = 0;

  World(MemoryArena& trans_arena, render::VulkanRenderer& renderer, asset::AssetSystem& assets,
        BlockRegistry& block_registry);

  inline u32 GetChunkCacheIndex(s32 v) {
    return ((v % (s32)kChunkCacheSize) + (s32)kChunkCacheSize) % (s32)kChunkCacheSize;
  }

  inline float GetCelestialAngle() const {
    float result = (((s32)world_tick - 6000) % 24000) / 24000.0f;

    if (result < 0.0f) result += 1.0f;
    if (result > 1.0f) result -= 1.0f;

    return result;
  }

  inline float GetSunlight() const {
    float angle = GetCelestialAngle();
    float sunlight = 1.0f - (cosf(angle * 3.1415f * 2.0f) * 2.0f + 1.0f);

    sunlight = 1.0f - Clamp(sunlight, 0.0f, 1.0f);

    return sunlight * 0.8f + 0.2f;
  }

  void Update(float dt);

  void OnDimensionChange();
  void OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid);
  void OnChunkLoad(s32 chunk_x, s32 chunk_z);
  void OnChunkUnload(s32 chunk_x, s32 chunk_z);

  void BuildChunkMesh(render::ChunkBuildContext* ctx);
  void BuildChunkMesh(render::ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_y, s32 chunk_z);
  void EnqueueChunk(render::ChunkBuildContext* ctx, s32 chunk_y);
  void FreeMeshes();
};

} // namespace world
} // namespace polymer

#endif
