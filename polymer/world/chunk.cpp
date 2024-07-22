#include "chunk.h"

#include <polymer/world/world.h>

namespace polymer {
namespace world {

struct ChunkOffset {
  s32 x = 0;
  s32 y = 0;
  s32 z = 0;

  constexpr ChunkOffset() {}
  constexpr ChunkOffset(s32 x, s32 y, s32 z) : x(x), y(y), z(z) {}
};

constexpr ChunkOffset kOffsets[] = {
    ChunkOffset(0, -1, 0), ChunkOffset(0, 1, 0),  ChunkOffset(0, 0, -1),
    ChunkOffset(0, 0, 1),  ChunkOffset(-1, 0, 0), ChunkOffset(1, 0, 0),
};

void ChunkConnectivityGraph::Update(MemoryArena& trans_arena, const World& world, const Camera& camera) {
  visible_count = 0;

  VisibleChunk* start_chunk = visible_set + visible_count++;

  start_chunk->chunk_x = (s32)floorf(camera.position.x / 16.0f);
  start_chunk->chunk_y = (s32)floorf(camera.position.y / 16.0f) + 4;
  start_chunk->chunk_z = (s32)floorf(camera.position.z / 16.0f);

  std::bitset<kChunkCacheSize * kChunkCacheSize * kChunkColumnCount> view_set;

  MemoryRevert revert = trans_arena.GetReverter();

  struct ProcessChunk {
    s32 chunk_x;
    s32 chunk_y;
    s32 chunk_z;
    u16 from;
    u16 dirs;
  };

  ProcessChunk* process_queue = memory_arena_push_type(&trans_arena, ProcessChunk);
  size_t process_queue_size = 0;

  process_queue[process_queue_size++] = {start_chunk->chunk_x, start_chunk->chunk_y, start_chunk->chunk_z,
                                         (u16)BlockFace::Down, (u16)0};

  Frustum frustum = camera.GetViewFrustum();

  size_t process_index = 0;
  while (process_index < process_queue_size) {
    ProcessChunk process_chunk = process_queue[process_index];

    size_t x_index = world::GetChunkCacheIndex(process_chunk.chunk_x);
    size_t z_index = world::GetChunkCacheIndex(process_chunk.chunk_z);

    ChunkConnectivitySet& connect_set = this->chunk_connectivity[z_index][x_index][process_chunk.chunk_y];

    for (size_t i = 0; i < 6; ++i) {
      BlockFace through_face = (BlockFace)i;

      ChunkOffset offset = kOffsets[i];

      s32 chunk_x = process_chunk.chunk_x + offset.x;
      s32 chunk_y = process_chunk.chunk_y + offset.y;
      s32 chunk_z = process_chunk.chunk_z + offset.z;

      if (chunk_y < 0 || chunk_y >= kChunkColumnCount) continue;

      size_t new_x_index = world::GetChunkCacheIndex(chunk_x);
      size_t new_z_index = world::GetChunkCacheIndex(chunk_z);

      if (!world.chunk_infos[new_z_index][new_x_index].loaded) continue;

      if ((process_index == 0 && connect_set.HasFaceConnectivity(through_face)) ||
          connect_set.IsConnected((BlockFace)process_chunk.from, through_face)) {

        BlockFace from = GetOppositeFace(through_face);

        size_t view_index = (size_t)new_z_index * kChunkCacheSize * kChunkColumnCount +
                            (size_t)new_x_index * kChunkColumnCount + chunk_y;
        if (!view_set.test(view_index)) {
          view_set.set(view_index);

          Vector3f chunk_min(chunk_x * 16.0f, chunk_y * 16.0f - 64.0f, chunk_z * 16.0f);
          Vector3f chunk_max(chunk_x * 16.0f + 16.0f, chunk_y * 16.0f - 48.0f, chunk_z * 16.0f + 16.0f);

          if (frustum.Intersects(chunk_min, chunk_max)) {

            if (world.chunk_infos[new_z_index][new_x_index].bitmask & (1 << chunk_y)) {
              VisibleChunk* next_chunk = visible_set + visible_count++;

              next_chunk->chunk_x = chunk_x;
              next_chunk->chunk_y = chunk_y;
              next_chunk->chunk_z = chunk_z;
            }

            BlockFace opposite_face = GetOppositeFace(through_face);

            if (!(process_chunk.dirs & (1 << (u16)opposite_face))) {
              trans_arena.Allocate(sizeof(ProcessChunk), 1);
              process_queue[process_queue_size++] = {chunk_x, chunk_y, chunk_z, (u16)from,
                                                     (u16)(process_chunk.dirs | (1 << i))};
            }
          }
        }
      }
    }

    ++process_index;
  }
}

void ChunkConnectivityGraph::Build(const World& world, const Chunk* chunk, size_t x_index, size_t z_index,
                                   s32 chunk_y) {
  if (chunk) {
    chunk_connectivity[z_index][x_index][chunk_y].Build(world, *chunk);
  } else {
    chunk_connectivity[z_index][x_index][chunk_y].connectivity.set();
  }
}

bool ChunkConnectivitySet::Build(const World& world, const Chunk& chunk) {
  VisitSet visited;

  std::bitset<36> old_connectivity = connectivity;

  this->Clear();

  for (s8 y = 0; y < 16; ++y) {
    for (s8 z = 0; z < 16; ++z) {
      for (s8 x = 0; x < 16; ++x) {
        // Only begin flood fills from the outer edges because inside doesn't matter.
        if (!(x == 0 || y == 0 || z == 0 || x == 15 || y == 15 || z == 15)) continue;

        u32 bid = chunk.blocks[y][z][x];
        BlockModel& model = world.block_registry.states[bid].model;

        if (!(model.element_count == 0 || model.HasTransparency())) continue;

        if (!visited.test((size_t)z * 16 * 16 + (size_t)y * 16 + (size_t)x)) {
          u8 current_set = FloodFill(world, chunk, visited, x, y, z);

          for (size_t i = 0; i < 6; ++i) {
            if (current_set & (1 << i)) {
              for (size_t j = 0; j < 6; ++j) {
                if (current_set & (1 << j)) {
                  // Set connectivity to both faces if they were found to be connected in a single flood fill set.
                  connectivity.set(i * 6 + j);
                  connectivity.set(j * 6 + i);
                }
              }
            }
          }
        }
      }
    }
  }

  return old_connectivity != connectivity;
}

u8 ChunkConnectivitySet::FloodFill(const World& world, const Chunk& chunk, VisitSet& visited, s8 start_x, s8 start_y,
                                   s8 start_z) {
  struct Coord {
    s8 x;
    s8 y;
    s8 z;
    s8 pad;
  };

  Coord queue[16 * 16 * 16];
  size_t queue_count = 0;
  size_t queue_index = 0;

  queue[queue_count++] = {start_x, start_y, start_z};

  VisitSet queue_set;

  u8 current_set = 0;

  queue_set.set((size_t)(start_z) * 16 * 16 + (size_t)(start_y) * 16 + (size_t)(start_x));

  while (queue_index < queue_count) {
    s8 x = queue[queue_index].x;
    s8 y = queue[queue_index].y;
    s8 z = queue[queue_index].z;

    ++queue_index;

    u32 bid = chunk.blocks[y][z][x];
    BlockModel& model = world.block_registry.states[bid].model;

    if (!(model.element_count == 0 || model.HasTransparency())) continue;

    bool outside = x <= 0 || y <= 0 || z <= 0 || x >= 15 || y >= 15 || z >= 15;

    if (outside) {
      if (x <= 0) {
        current_set |= (1 << (u8)BlockFace::West);
      } else if (x >= 15) {
        current_set |= (1 << (u8)BlockFace::East);
      }

      if (y <= 0) {
        current_set |= (1 << (u8)BlockFace::Down);
      } else if (y >= 15) {
        current_set |= (1 << (u8)BlockFace::Up);
      }

      if (z <= 0) {
        current_set |= (1 << (u8)BlockFace::North);
      } else if (z >= 15) {
        current_set |= (1 << (u8)BlockFace::South);
      }
    }

    if (!visited.test((size_t)z * 16 * 16 + (size_t)y * 16 + (size_t)x)) {
      visited.set((size_t)z * 16 * 16 + (size_t)y * 16 + (size_t)x);

      if (x > 0 && !queue_set.test((size_t)(z) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x - 1))) {
        queue[queue_count++] = {(s8)(x - 1), y, z};
        queue_set.set((size_t)(z) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x - 1));
      }

      if (x < 15 && !queue_set.test((size_t)(z) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x + 1))) {
        queue[queue_count++] = {(s8)(x + 1), y, z};
        queue_set.set((size_t)(z) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x + 1));
      }

      if (y > 0 && !queue_set.test((size_t)(z) * 16 * 16 + (size_t)(y - 1) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, (s8)(y - 1), z};
        queue_set.set((size_t)(z) * 16 * 16 + (size_t)(y - 1) * 16 + (size_t)(x));
      }

      if (y < 15 && !queue_set.test((size_t)(z) * 16 * 16 + (size_t)(y + 1) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, (s8)(y + 1), z};
        queue_set.set((size_t)(z) * 16 * 16 + (size_t)(y + 1) * 16 + (size_t)(x));
      }

      if (z > 0 && !queue_set.test((size_t)(z - 1) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, y, (s8)(z - 1)};
        queue_set.set((size_t)(z - 1) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x));
      }

      if (z < 15 && !queue_set.test((size_t)(z + 1) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, y, (s8)(z + 1)};
        queue_set.set((size_t)(z + 1) * 16 * 16 + (size_t)(y) * 16 + (size_t)(x));
      }
    }
  }

  return current_set;
}

} // namespace world
} // namespace polymer
