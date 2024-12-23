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

void ChunkConnectivityGraph::Update(MemoryArena& trans_arena, World& world, const Camera& camera) {
  visible_count = 0;

  VisibleChunk* start_chunk = visible_set + visible_count++;

  start_chunk->chunk_x = (s32)floorf(camera.position.x / 16.0f);
  start_chunk->chunk_y = (s32)floorf(camera.position.y / 16.0f) + 4;
  start_chunk->chunk_z = (s32)floorf(camera.position.z / 16.0f);

  std::bitset<kChunkCacheSize * kChunkCacheSize * kChunkColumnCount> view_set;

  size_t view_index = (size_t)world::GetChunkCacheIndex(start_chunk->chunk_z) * kChunkCacheSize * kChunkColumnCount +
                      (size_t)world::GetChunkCacheIndex(start_chunk->chunk_x) * kChunkColumnCount +
                      start_chunk->chunk_y;
  view_set.set(view_index);

  MemoryRevert revert = trans_arena.GetReverter();

  struct ProcessChunk {
    s32 chunk_x;
    s32 chunk_y;
    s32 chunk_z;
    u16 from;
    u16 traversed;
  };

  // Record the visit state for each chunk, so they are only visited once from each direction
  struct VisitState {
    u8 directions;

    inline bool CanVisit(BlockFace through_face) const {
      return !(directions & (1 << (u8)through_face));
    }

    inline void VisitThrough(BlockFace through_face) {
      directions |= (1 << (u8)through_face);
    }
  };

  constexpr size_t kVisitStateCount = kChunkCacheSize * kChunkCacheSize * kChunkColumnCount;

  VisitState* visit_states = memory_arena_push_type_count(&trans_arena, VisitState, kVisitStateCount);
  memset(visit_states, 0, sizeof(VisitState) * kVisitStateCount);

  ProcessChunk* process_queue = memory_arena_push_type(&trans_arena, ProcessChunk);
  size_t process_queue_size = 0;

  process_queue[process_queue_size++] = {start_chunk->chunk_x, start_chunk->chunk_y, start_chunk->chunk_z, (u16)0xFF,
                                         0};

  Frustum frustum = camera.GetViewFrustum();

  size_t largest_queue_size = 1;

  while (process_queue_size > 0) {
    // Pop front of queue and replace it with the last item so it doesn't grow forever.
    ProcessChunk process_chunk = process_queue[0];
    process_queue[0] = process_queue[--process_queue_size];

    size_t x_index = world::GetChunkCacheIndex(process_chunk.chunk_x);
    size_t z_index = world::GetChunkCacheIndex(process_chunk.chunk_z);

    ChunkConnectivitySet& connect_set = this->chunk_connectivity[z_index][x_index][process_chunk.chunk_y];
    ChunkSectionInfo& info = world.chunk_infos[z_index][x_index];

    if (info.bitmask & (1 << process_chunk.chunk_y)) {
      // We are a non-empty chunk, so check if we are dirty.
      bool is_dirty = info.dirty_connectivity_set & (1 << process_chunk.chunk_y);
      bool is_loaded = world.chunks[z_index][x_index].chunks[process_chunk.chunk_y];

      if (is_dirty && is_loaded) {
        connect_set.Build(world, *world.chunks[z_index][x_index].chunks[process_chunk.chunk_y]);
        info.dirty_connectivity_set &= ~(1 << process_chunk.chunk_y);
      }
    } else {
      connect_set.connectivity.set();
    }

    for (size_t i = 0; i < 6; ++i) {
      BlockFace through_face = (BlockFace)i;
      ChunkOffset offset = kOffsets[i];
      BlockFace opposite_face = GetOppositeFace(through_face);

      // Each processed child can only go in one direction along each axis, so check if the opposite side has been
      // traversed.
      if (process_chunk.traversed & (1 << (u16)opposite_face)) continue;

      s32 chunk_x = process_chunk.chunk_x + offset.x;
      s32 chunk_y = process_chunk.chunk_y + offset.y;
      s32 chunk_z = process_chunk.chunk_z + offset.z;

      if (chunk_y < 0 || chunk_y >= kChunkColumnCount) continue;

      size_t new_x_index = world::GetChunkCacheIndex(chunk_x);
      size_t new_z_index = world::GetChunkCacheIndex(chunk_z);

      VisitState* visit_state =
          visit_states + chunk_y * kChunkCacheSize * kChunkCacheSize + new_z_index * kChunkCacheSize + new_x_index;
      if (!visit_state->CanVisit(through_face)) continue;

      ChunkSectionInfo& new_info = world.chunk_infos[new_z_index][new_x_index];

      if (!new_info.loaded) continue;

      // Always travel through camera-connected chunks.
      bool is_camera_connected = (process_chunk.from == 0xFF && connect_set.HasFaceConnectivity(through_face));
      // If we can go from the 'from' side that reached here to the new through-side, then we might be able to see
      // through this chunk.
      bool visibility_potential =
          process_chunk.from != 0xFF && connect_set.IsConnected(through_face, (BlockFace)process_chunk.from);

      if (is_camera_connected || visibility_potential) {
        Vector3f chunk_min(chunk_x * 16.0f, chunk_y * 16.0f - 64.0f, chunk_z * 16.0f);
        Vector3f chunk_max(chunk_x * 16.0f + 16.0f, chunk_y * 16.0f - 48.0f, chunk_z * 16.0f + 16.0f);

        if (frustum.Intersects(chunk_min, chunk_max)) {
          size_t view_index = (size_t)new_z_index * kChunkCacheSize * kChunkColumnCount +
                              (size_t)new_x_index * kChunkColumnCount + chunk_y;

          // Only add each chunk to the visibility set once.
          if (!view_set.test(view_index)) {
            view_set.set(view_index);

            if (world.chunk_infos[new_z_index][new_x_index].bitmask & (1 << chunk_y)) {
              VisibleChunk* next_chunk = visible_set + visible_count++;

              next_chunk->chunk_x = chunk_x;
              next_chunk->chunk_y = chunk_y;
              next_chunk->chunk_z = chunk_z;
            }
          }

          if (process_queue_size >= largest_queue_size) {
            largest_queue_size = process_queue_size + 1;
            trans_arena.Allocate(sizeof(ProcessChunk), 1);
          }

          u16 new_traversed = process_chunk.traversed | (1 << (u16)through_face);
          process_queue[process_queue_size++] = {chunk_x, chunk_y, chunk_z, (u16)opposite_face, new_traversed};

          visit_state->VisitThrough(through_face);
        }
      }
    }
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

  MemoryRevert revert = world.trans_arena.GetReverter();
  // Allocate a buffer that can be used for every flood fill instead of being on the stack.
  Coord* queue = memory_arena_push_type_count(&world.trans_arena, Coord, 16 * 16 * 16);

  for (s8 y = 0; y < 16; ++y) {
    for (s8 z = 0; z < 16; ++z) {
      for (s8 x = 0; x < 16; ++x) {
        // Only begin flood fills from the outer edges because inside doesn't matter.
        if (!(x == 0 || y == 0 || z == 0 || x == 15 || y == 15 || z == 15)) continue;

        u32 bid = chunk.blocks[y][z][x];
        BlockModel& model = world.block_registry.states[bid].model;

        if (!(model.element_count == 0 || model.HasTransparency())) continue;

        if (!visited.test((size_t)y * 16 * 16 + (size_t)z * 16 + (size_t)x)) {
          u8 current_set = FloodFill(world, chunk, visited, queue, x, y, z);

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

u8 ChunkConnectivitySet::FloodFill(const World& world, const Chunk& chunk, VisitSet& visited, Coord* queue, s8 start_x,
                                   s8 start_y, s8 start_z) {
  size_t queue_count = 0;
  size_t queue_index = 0;

  queue[queue_count++] = {start_x, start_y, start_z};

  VisitSet queue_set;

  u8 current_set = 0;

  queue_set.set((size_t)(start_y) * 16 * 16 + (size_t)(start_z) * 16 + (size_t)(start_x));

  while (queue_index < queue_count) {
    s8 x = queue[queue_index].x;
    s8 y = queue[queue_index].y;
    s8 z = queue[queue_index].z;

    ++queue_index;

    u32 bid = chunk.blocks[y][z][x];
    BlockModel& model = world.block_registry.states[bid].model;

    if (model.is_cube && !model.HasTransparency()) continue;

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

    if (!visited.test((size_t)y * 16 * 16 + (size_t)z * 16 + (size_t)x)) {
      visited.set((size_t)y * 16 * 16 + (size_t)z * 16 + (size_t)x);

      if (x > 0 && !queue_set.test((size_t)(y) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x - 1))) {
        queue[queue_count++] = {(s8)(x - 1), y, z};
        queue_set.set((size_t)(y) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x - 1));
      }

      if (x < 15 && !queue_set.test((size_t)(y) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x + 1))) {
        queue[queue_count++] = {(s8)(x + 1), y, z};
        queue_set.set((size_t)(y) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x + 1));
      }

      if (y > 0 && !queue_set.test((size_t)(y - 1) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, (s8)(y - 1), z};
        queue_set.set((size_t)(y - 1) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x));
      }

      if (y < 15 && !queue_set.test((size_t)(y + 1) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, (s8)(y + 1), z};
        queue_set.set((size_t)(y + 1) * 16 * 16 + (size_t)(z) * 16 + (size_t)(x));
      }

      if (z > 0 && !queue_set.test((size_t)(y) * 16 * 16 + (size_t)(z - 1) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, y, (s8)(z - 1)};
        queue_set.set((size_t)(y) * 16 * 16 + (size_t)(z - 1) * 16 + (size_t)(x));
      }

      if (z < 15 && !queue_set.test((size_t)(y) * 16 * 16 + (size_t)(z + 1) * 16 + (size_t)(x))) {
        queue[queue_count++] = {x, y, (s8)(z + 1)};
        queue_set.set((size_t)(y) * 16 * 16 + (size_t)(z + 1) * 16 + (size_t)(x));
      }
    }
  }

  return current_set;
}

} // namespace world
} // namespace polymer
