#ifndef POLYMER_RENDER_BLOCKMESHER_H_
#define POLYMER_RENDER_BLOCKMESHER_H_

#include <polymer/asset/asset_system.h>
#include <polymer/memory.h>
#include <polymer/render/chunk_renderer.h>
#include <polymer/types.h>

namespace polymer {

namespace world {

struct BlockRegistry;
struct ChunkSection;
struct World;

} // namespace world

namespace render {

struct ChunkBuildContext {
  s32 chunk_x;
  s32 chunk_z;

  u32 x_index = 0;
  u32 z_index = 0;

  world::ChunkSection* section = nullptr;
  world::ChunkSection* east_section = nullptr;
  world::ChunkSection* west_section = nullptr;
  world::ChunkSection* north_section = nullptr;
  world::ChunkSection* south_section = nullptr;
  world::ChunkSection* south_east_section = nullptr;
  world::ChunkSection* south_west_section = nullptr;
  world::ChunkSection* north_east_section = nullptr;
  world::ChunkSection* north_west_section = nullptr;

  ChunkBuildContext(s32 chunk_x, s32 chunk_z) : chunk_x(chunk_x), chunk_z(chunk_z) {}

  bool IsBuildable() const;
  bool GetNeighbors(world::World* world);
};

struct ChunkVertexData {
  u8* vertices[render::kRenderLayerCount];
  size_t vertex_count[render::kRenderLayerCount];

  u16* indices[render::kRenderLayerCount];
  size_t index_count[render::kRenderLayerCount];

  ChunkVertexData() {
    for (size_t i = 0; i < render::kRenderLayerCount; ++i) {
      vertices[i] = nullptr;
      vertex_count[i] = 0;

      indices[i] = nullptr;
      index_count[i] = 0;
    }
  }

  inline void SetVertices(render::RenderLayer layer, u8* new_vertices, size_t new_vertex_count) {
    vertices[(size_t)layer] = new_vertices;
    vertex_count[(size_t)layer] = new_vertex_count;
  }

  inline void SetIndices(render::RenderLayer layer, u16* new_indices, size_t new_index_count) {
    indices[(size_t)layer] = new_indices;
    index_count[(size_t)layer] = new_index_count;
  }
};

struct BlockMesherMapping {
  world::BlockIdRange water_range;
  world::BlockIdRange kelp_range;
  world::BlockIdRange seagrass_range;
  world::BlockIdRange tall_seagrass_range;
  world::BlockIdRange lava_range;
  world::BlockIdRange lily_pad_range;
  world::BlockIdRange cave_air_range;
  world::BlockIdRange void_air_range;
  world::BlockIdRange dirt_path_range;

  void Initialize(world::BlockRegistry& registry) {
    Load(registry, POLY_STR("minecraft:water"), &water_range);
    Load(registry, POLY_STR("minecraft:kelp"), &kelp_range);
    Load(registry, POLY_STR("minecraft:seagrass"), &seagrass_range);
    Load(registry, POLY_STR("minecraft:tall_seagrass"), &tall_seagrass_range);
    Load(registry, POLY_STR("minecraft:lava"), &lava_range);
    Load(registry, POLY_STR("minecraft:lily_pad"), &lily_pad_range);
    Load(registry, POLY_STR("minecraft:cave_air"), &cave_air_range);
    Load(registry, POLY_STR("minecraft:void_air"), &void_air_range);
    Load(registry, POLY_STR("minecraft:dirt_path"), &dirt_path_range);
  }

private:
  inline void Load(world::BlockRegistry& registry, const String& str, world::BlockIdRange* out) {
    world::BlockIdRange* range = registry.name_map.Find(str);

    if (range) {
      *out = *range;
    }
  }
};

struct BlockMesher {
  MemoryArena& trans_arena;
  asset::AssetSystem& assets;
  world::BlockRegistry& block_registry;

  MemoryArena vertex_arenas[render::kRenderLayerCount];
  MemoryArena index_arenas[render::kRenderLayerCount];

  BlockMesherMapping mapping;

  BlockMesher(MemoryArena& trans_arena, asset::AssetSystem& assets, world::BlockRegistry& block_registry)
      : trans_arena(trans_arena), assets(assets), block_registry(block_registry) {
    for (size_t i = 0; i < render::kRenderLayerCount; ++i) {
      vertex_arenas[i] = CreateArena(Megabytes(16));
      index_arenas[i] = CreateArena(Megabytes(4));
    }
  }

  ~BlockMesher() {
    for (size_t i = 0; i < render::kRenderLayerCount; ++i) {
      vertex_arenas[i].Destroy();
      index_arenas[i].Destroy();
    }
  }

  void Reset() {
    for (size_t i = 0; i < render::kRenderLayerCount; ++i) {
      vertex_arenas[i].Reset();
      index_arenas[i].Reset();
    }
  }

  ChunkVertexData CreateMesh(ChunkBuildContext* ctx, s32 chunk_y);
};

} // namespace render
} // namespace polymer

#endif
