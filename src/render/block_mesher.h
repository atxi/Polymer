#ifndef POLYMER_RENDER_BLOCKMESHER_H_
#define POLYMER_RENDER_BLOCKMESHER_H_

#include "../memory.h"
#include "../types.h"
#include "../world.h"

namespace polymer {

struct BlockRegistry;

namespace render {

struct ChunkBuildQueue {
  size_t count;
  ChunkCoord data[1024];

  inline void Enqueue(s32 chunk_x, s32 chunk_z) {
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

  inline bool IsInQueue(s32 chunk_x, s32 chunk_z) {
    for (size_t i = 0; i < count; ++i) {
      if (data[i].x == chunk_x && data[i].z == chunk_z) {
        return true;
      }
    }

    return false;
  }

  inline void Clear() {
    count = 0;
  }
};

struct ChunkBuildContext {
  s32 chunk_x;
  s32 chunk_z;

  u32 x_index = 0;
  u32 z_index = 0;

  ChunkSection* section = nullptr;
  ChunkSection* east_section = nullptr;
  ChunkSection* west_section = nullptr;
  ChunkSection* north_section = nullptr;
  ChunkSection* south_section = nullptr;
  ChunkSection* south_east_section = nullptr;
  ChunkSection* south_west_section = nullptr;
  ChunkSection* north_east_section = nullptr;
  ChunkSection* north_west_section = nullptr;

  ChunkBuildContext(s32 chunk_x, s32 chunk_z) : chunk_x(chunk_x), chunk_z(chunk_z) {}

  bool IsBuildable() {
    return (east_section->info->loaded && east_section->info->x == chunk_x + 1 && east_section->info->z == chunk_z) &&
           (west_section->info->loaded && west_section->info->x == chunk_x - 1 && west_section->info->z == chunk_z) &&
           (north_section->info->loaded && north_section->info->z == chunk_z - 1 &&
            north_section->info->x == chunk_x) &&
           (south_section->info->loaded && south_section->info->z == chunk_z + 1 &&
            south_section->info->x == chunk_x) &&
           (south_east_section->info->loaded && south_east_section->info->z == chunk_z + 1 &&
            south_east_section->info->x == chunk_x + 1) &&
           (south_west_section->info->loaded && south_west_section->info->z == chunk_z + 1 &&
            south_west_section->info->x == chunk_x - 1) &&
           (north_east_section->info->loaded && north_east_section->info->z == chunk_z - 1 &&
            north_east_section->info->x == chunk_x + 1) &&
           (north_west_section->info->loaded && north_west_section->info->z == chunk_z - 1 &&
            north_west_section->info->x == chunk_x - 1);
  }

  bool GetNeighbors(World* world) {
    x_index = world->GetChunkCacheIndex(chunk_x);
    z_index = world->GetChunkCacheIndex(chunk_z);

    u32 xeast_index = world->GetChunkCacheIndex(chunk_x + 1);
    u32 xwest_index = world->GetChunkCacheIndex(chunk_x - 1);
    u32 znorth_index = world->GetChunkCacheIndex(chunk_z - 1);
    u32 zsouth_index = world->GetChunkCacheIndex(chunk_z + 1);

    section = &world->chunks[z_index][x_index];
    east_section = &world->chunks[z_index][xeast_index];
    west_section = &world->chunks[z_index][xwest_index];
    north_section = &world->chunks[znorth_index][x_index];
    south_section = &world->chunks[zsouth_index][x_index];
    south_east_section = &world->chunks[zsouth_index][xeast_index];
    south_west_section = &world->chunks[zsouth_index][xwest_index];
    north_east_section = &world->chunks[znorth_index][xeast_index];
    north_west_section = &world->chunks[znorth_index][xwest_index];

    return IsBuildable();
  }
};

struct ChunkMesh {
  u8* vertices;
  size_t vertex_count;

  ChunkMesh() : vertices(nullptr), vertex_count(0) {}
};

// TODO: This could be a standalone function, but I wanted to create a struct in preparation for each mesher creating
// their own arena to make multithreaded meshing easier.
struct BlockMesher {
  BlockMesher(MemoryArena& arena) : arena(arena) {}

  MemoryArena& arena;

  ChunkMesh CreateMesh(BlockRegistry& block_registry, ChunkBuildContext* ctx, s32 chunk_y);
};

} // namespace render
} // namespace polymer

#endif
