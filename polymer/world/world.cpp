#include "world.h"

namespace polymer {
namespace world {

World::World(MemoryArena& trans_arena, render::VulkanRenderer& renderer, asset::AssetSystem& assets,
             BlockRegistry& block_registry)
    : trans_arena(trans_arena), renderer(renderer), block_mesher(trans_arena, assets, block_registry) {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSection* section = &chunks[chunk_z][chunk_x];
      ChunkSectionInfo* section_info = &chunk_infos[chunk_z][chunk_x];
      ChunkMesh* meshes = this->meshes[chunk_z][chunk_x];

      section->info = section_info;

      for (u32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        section->chunks[chunk_y] = nullptr;

        for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
          meshes[chunk_y].meshes[i].vertex_count = 0;
        }
      }
    }
  }
}

void World::Update(float dt) {
  if (!build_queue.dirty) return;

  for (size_t i = 0; i < build_queue.count;) {
    s32 chunk_x = build_queue.data[i].x;
    s32 chunk_z = build_queue.data[i].z;

    render::ChunkBuildContext ctx(chunk_x, chunk_z);

    if (ctx.GetNeighbors(this)) {
      BuildChunkMesh(&ctx);
      ctx.section->info->ClearQueued();
      build_queue.data[i] = build_queue.data[--build_queue.count];
    } else {
      ++i;
    }
  }

  build_queue.dirty = false;
}

void World::OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid) {
  s32 chunk_x = (s32)floorf(x / 16.0f);
  s32 chunk_z = (s32)floorf(z / 16.0f);
  s32 chunk_y = (s32)floorf(y / 16.0f) + 4;

  u32 x_index = GetChunkCacheIndex(chunk_x);
  u32 z_index = GetChunkCacheIndex(chunk_z);

  ChunkSection* section = &chunks[z_index][x_index];

  if (!section->info->loaded || (section->info->x != chunk_x || section->info->z != chunk_z)) return;

  s32 relative_x = x % 16;
  s32 relative_y = y % 16;
  s32 relative_z = z % 16;

  if (relative_x < 0) {
    relative_x += 16;
  }

  if (relative_y < 0) {
    relative_y += 16;
  }

  if (relative_z < 0) {
    relative_z += 16;
  }

  u32 old_bid = 0;

  if (section->chunks[chunk_y]) {
    old_bid = section->chunks[chunk_y]->blocks[relative_y][relative_z][relative_x];
  }

  if (new_bid != 0) {
    ChunkSectionInfo* section_info = &chunk_infos[z_index][x_index];

    section_info->bitmask |= (1 << chunk_y);
    section_info->loaded = true;

    if (!section->chunks[chunk_y]) {
      section->chunks[chunk_y] = chunk_pool.Allocate();
    }
  }

  if (section->chunks[chunk_y]) {
    section->chunks[chunk_y]->blocks[relative_y][relative_z][relative_x] = (u32)new_bid;
  }

  EnqueueChunk(chunk_x, chunk_y, chunk_z);

  if (relative_x == 0) {
    // Rebuild west
    EnqueueChunk(chunk_x - 1, chunk_y, chunk_z);
  } else if (relative_x == 15) {
    // Rebuild east
    EnqueueChunk(chunk_x + 1, chunk_y, chunk_z);
  }

  if (relative_z == 0) {
    // Rebuild north
    EnqueueChunk(chunk_x, chunk_y, chunk_z - 1);
  } else if (relative_z == 15) {
    // Rebuild south
    EnqueueChunk(chunk_x, chunk_y, chunk_z + 1);
  }

  if (relative_y == 0 && chunk_y > 0) {
    // Rebuild below
    EnqueueChunk(chunk_x, chunk_y - 1, chunk_z);
  } else if (relative_y == 15 && chunk_y < 15) {
    // Rebuild above
    EnqueueChunk(chunk_x, chunk_y + 1, chunk_z);
  }
}

void World::OnChunkLoad(s32 chunk_x, s32 chunk_z) {
  u32 x_index = GetChunkCacheIndex(chunk_x);
  u32 z_index = GetChunkCacheIndex(chunk_z);

  ChunkSectionInfo* section_info = &chunk_infos[z_index][x_index];
  ChunkMesh* meshes = this->meshes[z_index][x_index];

  if (section_info->loaded) {
    if (section_info->x == chunk_x && section_info->z == chunk_z && section_info->IsQueued()) {
      printf("Got chunk again while in queue.\n");
      return;
    }

    printf("Got chunk %d, %d with existing chunk %d, %d.\n", chunk_x, chunk_z, section_info->x, section_info->z);
    renderer.WaitForIdle();

    build_queue.Dequeue(section_info->x, section_info->z);
    section_info->ClearQueued();

    // Force clear any existing meshes
    for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
      ChunkMesh* mesh = meshes + chunk_y;

      for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
        if (mesh->meshes[i].vertex_count > 0) {
          renderer.FreeMesh(&mesh->meshes[i]);
          mesh->meshes[i].vertex_count = 0;
        }
      }
    }
  }

  section_info->loaded = true;
  section_info->x = chunk_x;
  section_info->z = chunk_z;

  // TODO: Only queueing up chunks to be meshed when they are requested to be viewed would greatly reduce meshing time.
  // IMPORTANT: This should be high priority because newly-generated chunks can have fluids that are moving that will
  // cause unseen chunks to have to be re-meshed a bunch of times.
  // It might also be worth separating the fluid and solid block mesher so fluid block changes don't require the full
  // rebuild.
  if (!section_info->IsQueued()) {
    build_queue.Enqueue(chunk_x, chunk_z);
  }
  section_info->SetQueued();
}

void World::OnChunkUnload(s32 chunk_x, s32 chunk_z) {
  u32 x_index = GetChunkCacheIndex(chunk_x);
  u32 z_index = GetChunkCacheIndex(chunk_z);
  ChunkSection* section = &chunks[z_index][x_index];
  ChunkSectionInfo* section_info = &chunk_infos[z_index][x_index];

  build_queue.Dequeue(chunk_x, chunk_z);

  // It's possible to receive an unload packet after receiving a new chunk that would take this chunk's position in
  // the cache, so it needs to be checked before anything is changed in the cache.
  if (section_info->x != chunk_x || section_info->z != chunk_z) {
    return;
  }

  section_info->ClearQueued();
  section_info->loaded = false;
  section_info->bitmask = 0;
  occupy_set.ClearChunk(chunk_x, chunk_z);

  for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
    if (section->chunks[chunk_y]) {
      chunk_pool.Free(section->chunks[chunk_y]);
      section->chunks[chunk_y] = nullptr;
    }
  }

  ChunkMesh* meshes = this->meshes[z_index][x_index];

  renderer.WaitForIdle();

  for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
    for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
      if (meshes[chunk_y].meshes[i].vertex_count > 0) {
        renderer.FreeMesh(&meshes[chunk_y].meshes[i]);
        meshes[chunk_y].meshes[i].vertex_count = 0;
      }
    }
  }
}

void World::OnDimensionChange() {
  renderer.WaitForIdle();

  for (s32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (s32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSectionInfo* section_info = &chunk_infos[chunk_z][chunk_x];
      ChunkMesh* meshes = this->meshes[chunk_z][chunk_x];

      section_info->loaded = false;
      section_info->bitmask = 0;
      section_info->ClearQueued();

      for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        ChunkMesh* mesh = meshes + chunk_y;

        for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
          if (mesh->meshes[i].vertex_count > 0) {
            renderer.FreeMesh(&mesh->meshes[i]);
            mesh->meshes[i].vertex_count = 0;
          }
        }
      }
    }
  }

  occupy_set.Clear();
  build_queue.Clear();
}

void World::BuildChunkMesh(render::ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_y, s32 chunk_z) {
  MemoryRevert arena_revert = trans_arena.GetReverter();

  render::ChunkVertexData vertex_data = block_mesher.CreateMesh(ctx, chunk_y);

  ChunkMesh* meshes = this->meshes[ctx->z_index][ctx->x_index];

  for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
    if (meshes[chunk_y].meshes[i].vertex_count > 0) {
      renderer.WaitForIdle();
      renderer.FreeMesh(&meshes[chunk_y].meshes[i]);
      meshes[chunk_y].meshes[i].vertex_count = 0;
    }

    if (vertex_data.vertex_count[i] > 0) {
      assert(vertex_data.vertex_count[i] <= 0xFFFFFFFF);

      const size_t data_size = sizeof(render::ChunkVertex) * vertex_data.vertex_count[i];

      meshes[chunk_y].meshes[i].vertex_count = (u32)vertex_data.vertex_count[i];
      meshes[chunk_y].meshes[i] = renderer.AllocateMesh(vertex_data.vertices[i], data_size, vertex_data.vertex_count[i],
                                                        vertex_data.indices[i], vertex_data.index_count[i]);
    }
  }

  block_mesher.Reset();
}

void World::EnqueueChunk(s32 chunk_x, s32 chunk_y, s32 chunk_z) {
  u32 x_index = world::GetChunkCacheIndex(chunk_x);
  u32 z_index = world::GetChunkCacheIndex(chunk_z);

  ChunkSectionInfo* section_info = &chunk_infos[z_index][x_index];

  if (!section_info->IsQueued()) {
    build_queue.Enqueue(chunk_x, chunk_z);
  }

  section_info->SetQueued(chunk_y);
}

void World::BuildChunkMesh(render::ChunkBuildContext* ctx) {
  // TODO: This should probably be done on a separate thread
  ChunkSectionInfo* section_info = &chunk_infos[ctx->z_index][ctx->x_index];

  renderer.BeginMeshAllocation();

  ChunkMesh* meshes = this->meshes[ctx->z_index][ctx->x_index];

  for (s32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
    if (!(section_info->bitmask & (1 << chunk_y))) {
      for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
        meshes[chunk_y].meshes[i].vertex_count = 0;
      }

      continue;
    }

    if (section_info->IsQueued(chunk_y)) {
      BuildChunkMesh(ctx, ctx->chunk_x, chunk_y, ctx->chunk_z);
      occupy_set.SetChunk(ctx->chunk_x, ctx->chunk_z);
    }
  }

  section_info->ClearQueued();
  renderer.EndMeshAllocation();
}

void World::FreeMeshes() {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkMesh* meshes = this->meshes[chunk_z][chunk_x];

      for (u32 chunk_y = 0; chunk_y < kChunkColumnCount; ++chunk_y) {
        ChunkMesh* mesh = meshes + chunk_y;

        for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
          if (mesh->meshes[i].vertex_count > 0) {
            renderer.FreeMesh(&mesh->meshes[i]);
            mesh->meshes[i].vertex_count = 0;
          }
        }
      }
    }
  }
}

} // namespace world
} // namespace polymer
