#ifndef POLYMER_WORLD_H_
#define POLYMER_WORLD_H_

#include <polymer/memory.h>
#include <polymer/render/block_mesher.h>
#include <polymer/render/chunk_renderer.h>
#include <polymer/render/render.h>
#include <polymer/types.h>
#include <polymer/world/chunk.h>

namespace polymer {
namespace world {

struct World {
  // Store the chunk data separately to make render iteration faster
  ChunkSection chunks[kChunkCacheSize][kChunkCacheSize];
  ChunkSectionInfo chunk_infos[kChunkCacheSize][kChunkCacheSize];
  ChunkMesh meshes[kChunkCacheSize][kChunkCacheSize][kChunkColumnCount];
  ChunkConnectivityGraph connectivity_graph;

  BlockRegistry& block_registry;
  MemoryPool<Chunk> chunk_pool;
  render::BlockMesher block_mesher;

  MemoryArena& trans_arena;
  render::VulkanRenderer& renderer;

  u32 world_tick = 0;

  World(MemoryArena& trans_arena, render::VulkanRenderer& renderer, asset::AssetSystem& assets,
        BlockRegistry& block_registry);

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
  void EnqueueChunk(s32 chunk_x, s32 chunk_y, s32 chunk_z);
  void FreeMeshes();
};

} // namespace world
} // namespace polymer

#endif
