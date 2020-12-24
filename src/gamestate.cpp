#include "gamestate.h"

#include "json.h"
#include "math.h"
#include "stb_image.h"
#include "zip_archive.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <unordered_map>

namespace polymer {

inline void PushVertex(MemoryArena* arena, ChunkVertex* vertices, u32* count, const Vector3f& position,
                       const Vector2f& uv, u32 texture_id, u32 tintindex) {
  arena->Allocate(sizeof(ChunkVertex), 1);

  vertices[*count].position = position;
  vertices[*count].texcoord = uv;
  vertices[*count].texture_id = texture_id;
  vertices[*count].tint_index = tintindex;

  ++*count;
}

GameState::GameState(VulkanRenderer* renderer, MemoryArena* perm_arena, MemoryArena* trans_arena)
    : perm_arena(perm_arena), trans_arena(trans_arena), connection(*perm_arena), renderer(renderer) {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSection* section = &world.chunks[chunk_z][chunk_x];
      ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];

      section->info = section_info;

      RenderMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (u32 chunk_y = 0; chunk_y < 16; ++chunk_y) {
        meshes[chunk_y].vertex_count = 0;
      }
    }
  }

  camera.near = 0.1f;
  camera.far = 256.0f;
  camera.fov = Radians(80.0f);
}

void GameState::Update(float dt, InputState* input) {
  const float kMoveSpeed = 20.0f;
  const float kSprintModifier = 1.3f;

  Vector3f movement;

  if (input->forward) {
    movement += camera.GetForward();
  }

  if (input->backward) {
    movement -= camera.GetForward();
  }

  if (input->left) {
    Vector3f forward = camera.GetForward();
    Vector3f right = Normalize(forward.Cross(Vector3f(0, 1, 0)));
    movement -= right;
  }

  if (input->right) {
    Vector3f forward = camera.GetForward();
    Vector3f right = Normalize(forward.Cross(Vector3f(0, 1, 0)));
    movement += right;
  }

  if (input->climb) {
    movement += Vector3f(0, 1, 0);
  }

  if (input->fall) {
    movement -= Vector3f(0, 1, 0);
  }

  if (movement.LengthSq() > 0) {
    float modifier = kMoveSpeed;

    if (input->sprint) {
      modifier *= kSprintModifier;
    }

    camera.position += Normalize(movement) * (dt * modifier);
  }

  // Process build queue
  for (size_t i = 0; i < world.build_queue_count;) {
    s32 chunk_x = world.build_queue[i].x;
    s32 chunk_z = world.build_queue[i].z;

    ChunkBuildContext ctx(chunk_x, chunk_z);

    if (ctx.GetNeighbors(&world)) {
      BuildChunkMesh(&ctx, chunk_x, chunk_z);
      world.build_queue[i] = world.build_queue[--world.build_queue_count];
    } else {
      ++i;
    }
  }

  // Render game world
  camera.aspect_ratio = (float)renderer->swap_extent.width / renderer->swap_extent.height;

  UniformBufferObject ubo;
  void* data = nullptr;

  ubo.mvp = camera.GetProjectionMatrix() * camera.GetViewMatrix();

  vmaMapMemory(renderer->allocator, renderer->uniform_allocations[renderer->current_frame], &data);
  memcpy(data, ubo.mvp.data, sizeof(UniformBufferObject));
  vmaUnmapMemory(renderer->allocator, renderer->uniform_allocations[renderer->current_frame]);

  Frustum frustum = camera.GetViewFrustum();

  VkDeviceSize offsets[] = {0};

  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];

      if (!section_info->loaded) {
        continue;
      }

      RenderMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (u32 chunk_y = 0; chunk_y < 16; ++chunk_y) {
        RenderMesh* mesh = meshes + chunk_y;

        if (mesh->vertex_count > 0) {
          Vector3f chunk_min(section_info->x * 16.0f, chunk_y * 16.0f, section_info->z * 16.0f);
          Vector3f chunk_max(section_info->x * 16.0f + 16.0f, chunk_y * 16.0f + 16.0f, section_info->z * 16.0f + 16.0f);

          if (frustum.Intersects(chunk_min, chunk_max)) {
            vkCmdBindVertexBuffers(renderer->command_buffers[renderer->current_frame], 0, 1, &mesh->vertex_buffer,
                                   offsets);
            vkCmdDraw(renderer->command_buffers[renderer->current_frame], (u32)mesh->vertex_count, 1, 0, 0);
          }
        }
      }
    }
  }
}

void GameState::OnWindowMouseMove(s32 dx, s32 dy) {
  const float kSensitivity = 0.005f;
  const float kMaxPitch = Radians(89.0f);

  camera.yaw += dx * kSensitivity;
  camera.pitch -= dy * kSensitivity;

  if (camera.pitch > kMaxPitch) {
    camera.pitch = kMaxPitch;
  } else if (camera.pitch < -kMaxPitch) {
    camera.pitch = -kMaxPitch;
  }
}

void GameState::OnPlayerPositionAndLook(const Vector3f& position, float yaw, float pitch) {
  camera.position = position + Vector3f(0, 1.8f, 0);
  camera.yaw = Radians(yaw + 90.0f);
  camera.pitch = -Radians(pitch);
}

void GameState::OnChunkUnload(s32 chunk_x, s32 chunk_z) {
  u32 x_index = world.GetChunkCacheIndex(chunk_x);
  u32 z_index = world.GetChunkCacheIndex(chunk_z);
  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];

  section_info->loaded = false;

  if (section_info->x != chunk_x || section_info->z != chunk_z) {
    return;
  }

  RenderMesh* meshes = world.meshes[z_index][x_index];

  for (size_t chunk_y = 0; chunk_y < 16; ++chunk_y) {
    if (meshes[chunk_y].vertex_count > 0) {
      renderer->FreeMesh(meshes + chunk_y);
      meshes[chunk_y].vertex_count = 0;
    }
  }
}

void GameState::BuildChunkMesh(ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_y, s32 chunk_z) {
  u8* arena_snapshot = trans_arena->current;

  RenderMesh* meshes = world.meshes[ctx->z_index][ctx->x_index];

  u32* bordered_chunk = (u32*)trans_arena->Allocate(sizeof(u32) * 18 * 18 * 18);
  memset(bordered_chunk, 0, sizeof(u32) * 18 * 18 * 18);

  ChunkSection* section = ctx->section;
  ChunkSection* east_section = ctx->east_section;
  ChunkSection* west_section = ctx->west_section;
  ChunkSection* north_section = ctx->north_section;
  ChunkSection* south_section = ctx->south_section;

  // Create an initial pointer to transient memory with zero vertices allocated.
  // Each push will allocate a new vertex with just a stack pointer increase so it's quick and contiguous.
  ChunkVertex* vertices = (ChunkVertex*)trans_arena->Allocate(0);
  u32 vertex_count = 0;

  for (s64 y = 0; y < 16; ++y) {
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + (x + 1));
        bordered_chunk[index] = section->chunks[chunk_y].blocks[y][z][x];
      }
    }
  }

  // Load west blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + 0);
      bordered_chunk[index] = west_section->chunks[chunk_y].blocks[y][z][15];
    }
  }

  // Load east blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + 17);
      bordered_chunk[index] = east_section->chunks[chunk_y].blocks[y][z][0];
    }
  }

  // Load north blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)((y + 1) * 18 * 18 + (x + 1));
      bordered_chunk[index] = north_section->chunks[chunk_y].blocks[y][15][x];
    }
  }

  // Load south blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + (x + 1));
      bordered_chunk[index] = south_section->chunks[chunk_y].blocks[y][0][x];
    }
  }

  if (chunk_y < 16) {
    // Load above blocks
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + (x + 1));
        bordered_chunk[index] = section->chunks[chunk_y + 1].blocks[0][z][x];
      }
    }
  }

  if (chunk_y > 0) {
    // Load below blocks
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)((z + 1) * 18 + (x + 1));
        bordered_chunk[index] = section->chunks[chunk_y - 1].blocks[15][z][x];
      }
    }
  }

  Vector3f chunk_base(chunk_x * 16.0f, chunk_y * 16.0f, chunk_z * 16.0f);

  for (size_t relative_y = 0; relative_y < 16; ++relative_y) {
    for (size_t relative_z = 0; relative_z < 16; ++relative_z) {
      for (size_t relative_x = 0; relative_x < 16; ++relative_x) {
        size_t index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);

        u32 bid = bordered_chunk[index];

        // Skip air and barriers
        if (bid == 0 || bid == 7540) {
          continue;
        }

        size_t above_index = (relative_y + 2) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);
        size_t below_index = (relative_y + 0) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);
        size_t north_index = (relative_y + 1) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 1);
        size_t south_index = (relative_y + 1) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 1);
        size_t east_index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 2);
        size_t west_index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 0);

        u32 above_id = bordered_chunk[above_index];
        u32 below_id = bordered_chunk[below_index];
        u32 north_id = bordered_chunk[north_index];
        u32 south_id = bordered_chunk[south_index];
        u32 east_id = bordered_chunk[east_index];
        u32 west_id = bordered_chunk[west_index];

        float x = (float)relative_x;
        float y = (float)relative_y;
        float z = (float)relative_z;

        BlockModel* model = &block_states[bid].model;
        BlockModel* above_model = &block_states[above_id].model;
        BlockModel* below_model = &block_states[below_id].model;
        BlockModel* north_model = &block_states[north_id].model;
        BlockModel* south_model = &block_states[south_id].model;
        BlockModel* east_model = &block_states[east_id].model;
        BlockModel* west_model = &block_states[west_id].model;

        if (!above_model->IsOccluding()) {
          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 1;

            if (!face->render) continue;

            u32 texture_id = face->texture_id;
            u32 tintindex = face->tintindex;

            Vector3f& from = element->from;
            Vector3f& to = element->to;

            Vector3f bottom_left(x + from.x, y + to.y, z + from.z);
            Vector3f bottom_right(x + from.x, y + to.y, z + to.z);
            Vector3f top_left(x + to.x, y + to.y, z + from.z);
            Vector3f top_right(x + to.x, y + to.y, z + to.z);

            Vector2f bl_uv(face->uv_from.x, face->uv_from.y);
            Vector2f br_uv(face->uv_from.x, face->uv_to.y);
            Vector2f tr_uv(face->uv_to.x, face->uv_to.y);
            Vector2f tl_uv(face->uv_to.x, face->uv_from.y);

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
          }
        }

        if (!below_model->IsOccluding()) {
          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 0;

            if (!face->render) continue;

            u32 texture_id = face->texture_id;
            u32 tintindex = face->tintindex;

            Vector3f& from = element->from;
            Vector3f& to = element->to;

            Vector3f bottom_left(x + to.x, y + from.y, z + from.z);
            Vector3f bottom_right(x + to.x, y + from.y, z + to.z);
            Vector3f top_left(x + from.x, y + from.y, z + from.z);
            Vector3f top_right(x + from.x, y + from.y, z + to.z);

            Vector2f bl_uv(face->uv_to.x, face->uv_to.y);
            Vector2f br_uv(face->uv_to.x, face->uv_from.y);
            Vector2f tr_uv(face->uv_from.x, face->uv_from.y);
            Vector2f tl_uv(face->uv_from.x, face->uv_to.y);

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
          }
        }

        if (!north_model->IsOccluding()) {
          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 2;

            if (!face->render) continue;

            u32 texture_id = face->texture_id;
            u32 tintindex = face->tintindex;

            Vector3f& from = element->from;
            Vector3f& to = element->to;

            Vector3f bottom_left(x + to.x, y + from.y, z + from.z);
            Vector3f bottom_right(x + from.x, y + from.y, z + from.z);
            Vector3f top_left(x + to.x, y + to.y, z + from.z);
            Vector3f top_right(x + from.x, y + to.y, z + from.z);

            Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
            Vector2f br_uv(face->uv_to.x, face->uv_to.y);
            Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
            Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
          }
        }

        if (!south_model->IsOccluding()) {
          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 3;

            if (!face->render) continue;

            u32 texture_id = face->texture_id;
            u32 tintindex = face->tintindex;

            Vector3f& from = element->from;
            Vector3f& to = element->to;

            Vector3f bottom_left(x + from.x, y + from.y, z + to.z);
            Vector3f bottom_right(x + to.x, y + from.y, z + to.z);
            Vector3f top_left(x + from.x, y + to.y, z + to.z);
            Vector3f top_right(x + to.x, y + to.y, z + to.z);

            Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
            Vector2f br_uv(face->uv_to.x, face->uv_to.y);
            Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
            Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
          }
        }

        if (!east_model->IsOccluding()) {
          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 5;

            if (!face->render) continue;

            u32 texture_id = face->texture_id;
            u32 tintindex = face->tintindex;

            Vector3f& from = element->from;
            Vector3f& to = element->to;

            Vector3f bottom_left(x + to.x, y + from.y, z + to.z);
            Vector3f bottom_right(x + to.x, y + from.y, z + from.z);
            Vector3f top_left(x + to.x, y + to.y, z + to.z);
            Vector3f top_right(x + to.x, y + to.y, z + from.z);

            Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
            Vector2f br_uv(face->uv_to.x, face->uv_to.y);
            Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
            Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
          }
        }

        if (!west_model->IsOccluding()) {
          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 4;

            if (!face->render) continue;

            u32 texture_id = face->texture_id;
            u32 tintindex = face->tintindex;

            Vector3f& from = element->from;
            Vector3f& to = element->to;

            Vector3f bottom_left(x + from.x, y + from.y, z + from.z);
            Vector3f bottom_right(x + from.x, y + from.y, z + to.z);
            Vector3f top_left(x + from.x, y + to.y, z + from.z);
            Vector3f top_right(x + from.x, y + to.y, z + to.z);

            Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
            Vector2f br_uv(face->uv_to.x, face->uv_to.y);
            Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
            Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex);
          }
        }
      }
    }
  }

  if (vertex_count > 0) {
    meshes[chunk_y] = renderer->AllocateMesh((u8*)vertices, sizeof(ChunkVertex) * vertex_count, vertex_count);
  } else {
    meshes[chunk_y].vertex_count = 0;
  }

  // Reset the arena to where it was before this allocation. The data was already sent to the GPU so it's no longer
  // useful.
  trans_arena->current = arena_snapshot;
}

void GameState::BuildChunkMesh(ChunkBuildContext* ctx, s32 chunk_x, s32 chunk_z) {
  // TODO:
  // This should probably be done on a separate thread or in a compute shader ideally.
  // Either an index buffer or face merging should be done to reduce buffer size.

  u32 x_index = world.GetChunkCacheIndex(chunk_x);
  u32 z_index = world.GetChunkCacheIndex(chunk_z);

  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];

  ChunkSection* east_section = ctx->east_section;
  ChunkSection* west_section = ctx->west_section;
  ChunkSection* north_section = ctx->north_section;
  ChunkSection* south_section = ctx->south_section;

  for (s32 chunk_y = 0; chunk_y < 16; ++chunk_y) {
    if (!(section_info->bitmask & (1 << chunk_y))) {
      continue;
    }

    BuildChunkMesh(ctx, chunk_x, chunk_y, chunk_z);
  }
}

void GameState::OnDimensionChange() {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];
      RenderMesh* meshes = world.meshes[chunk_z][chunk_x];

      section_info->loaded = false;

      for (u32 chunk_y = 0; chunk_y < 16; ++chunk_y) {
        RenderMesh* mesh = meshes + chunk_y;

        if (mesh->vertex_count > 0) {
          renderer->FreeMesh(mesh);
        }

        mesh->vertex_count = 0;
      }
    }
  }

  world.build_queue_count = 0;
}

void GameState::OnChunkLoad(s32 chunk_x, s32 chunk_z) {
  u32 x_index = world.GetChunkCacheIndex(chunk_x);
  u32 z_index = world.GetChunkCacheIndex(chunk_z);

  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];

  section_info->loaded = true;

  world.build_queue[world.build_queue_count++] = {chunk_x, chunk_z};
}

void GameState::OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid) {
  s32 chunk_x = (s32)std::floor(x / 16.0f);
  s32 chunk_z = (s32)std::floor(z / 16.0f);
  s32 chunk_y = y / 16;

  ChunkSection* section = &world.chunks[world.GetChunkCacheIndex(chunk_z)][world.GetChunkCacheIndex(chunk_x)];

  s32 relative_x = x % 16;
  s32 relative_y = y % 16;
  s32 relative_z = z % 16;

  if (relative_x < 0) {
    relative_x += 16;
  }

  if (relative_z < 0) {
    relative_z += 16;
  }

  u32 old_bid = section->chunks[chunk_y].blocks[relative_y][relative_z][relative_x];

  section->chunks[chunk_y].blocks[relative_y][relative_z][relative_x] = (u32)new_bid;

  if (new_bid != 0) {
    ChunkSectionInfo* section_info =
        &world.chunk_infos[world.GetChunkCacheIndex(chunk_z)][world.GetChunkCacheIndex(chunk_x)];
    section_info->bitmask |= (1 << (y / 16));
  }

  bool is_queued = false;
  for (size_t i = 0; i < world.build_queue_count; ++i) {
    if (world.build_queue[i].x == chunk_x && world.build_queue[i].z == chunk_z) {
      is_queued = true;
      break;
    }
  }

  if (!is_queued) {
    ChunkBuildContext ctx(chunk_x, chunk_z);

    bool has_neighbors = ctx.GetNeighbors(&world);
    // If the chunk isn't currently queued then it must already be generated
    assert(has_neighbors);

    BuildChunkMesh(&ctx, chunk_x, chunk_y, chunk_z);
  }

#if 0
  printf("Block changed at (%d, %d, %d) from %s to %s\n", x, y, z, block_states[old_bid].name,
         block_states[new_bid].name);
#endif
}

void GameState::FreeMeshes() {
  for (u32 chunk_z = 0; chunk_z < kChunkCacheSize; ++chunk_z) {
    for (u32 chunk_x = 0; chunk_x < kChunkCacheSize; ++chunk_x) {
      RenderMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (u32 chunk_y = 0; chunk_y < 16; ++chunk_y) {
        RenderMesh* mesh = meshes + chunk_y;

        if (mesh->vertex_count > 0) {
          renderer->FreeMesh(mesh);
        }

        mesh->vertex_count = 0;
      }
    }
  }
}

BlockModel LoadModel(MemoryArena* arena, ZipArchive& zip, const char* path, size_t path_size,
                     std::unordered_map<std::string, std::string>& texture_map,
                     std::unordered_map<std::string, u32>& texture_ids) {
  BlockModel result = {};
  char full_path[256];

  sprintf(full_path, "assets/minecraft/models/block/%.*s.json", (u32)path_size, path);
  size_t size;
  char* data = zip.ReadFile(arena, full_path, &size);

  assert(data);

  json_value_s* root = json_parse(data, size);
  assert(root->type == json_type_object);

  json_object_s* root_obj = json_value_as_object(root);
  assert(root_obj);

  json_object_element_s* root_element = root_obj->start;
  // Do multiple loops over the elements in a specific order to simplify texture ids.

  while (root_element) {
    if (strcmp(root_element->name->string, "textures") == 0) {
      json_object_s* texture_obj = json_value_as_object(root_element->value);
      json_object_element_s* texture_element = texture_obj->start;

      while (texture_element) {
        json_string_s* value_obj = json_value_as_string(texture_element->value);

        std::string name(texture_element->name->string, texture_element->name->string_size);
        std::string value(value_obj->string, value_obj->string_size);

        texture_map[name] = value;

        texture_element = texture_element->next;
      }
    }
    root_element = root_element->next;
  }

  root_element = root_obj->start;
  while (root_element) {
    if (strcmp(root_element->name->string, "elements") == 0) {
      json_array_s* element_array = json_value_as_array(root_element->value);

      json_array_element_s* element_array_element = element_array->start;
      while (element_array_element) {
        json_object_s* element_obj = json_value_as_object(element_array_element->value);

        json_object_element_s* element_property = element_obj->start;
        while (element_property) {
          const char* property_name = element_property->name->string;

          if (strcmp(property_name, "from") == 0) {
            json_array_element_s* vector_element = json_value_as_array(element_property->value)->start;

            for (int i = 0; i < 3; ++i) {
              result.elements[result.element_count].from[i] =
                  strtol(json_value_as_number(vector_element->value)->number, nullptr, 10) / 16.0f;
              vector_element = vector_element->next;
            }
          } else if (strcmp(property_name, "to") == 0) {
            json_array_element_s* vector_element = json_value_as_array(element_property->value)->start;

            for (int i = 0; i < 3; ++i) {
              result.elements[result.element_count].to[i] =
                  strtol(json_value_as_number(vector_element->value)->number, nullptr, 10) / 16.0f;
              vector_element = vector_element->next;
            }
          } else if (strcmp(property_name, "faces") == 0) {
            json_object_element_s* face_obj_element = json_value_as_object(element_property->value)->start;
            while (face_obj_element) {
              const char* facename = face_obj_element->name->string;

              size_t face_index = 0;

              if (strcmp(facename, "down") == 0) {
                face_index = 0;
              } else if (strcmp(facename, "up") == 0) {
                face_index = 1;
              } else if (strcmp(facename, "north") == 0) {
                face_index = 2;
              } else if (strcmp(facename, "south") == 0) {
                face_index = 3;
              } else if (strcmp(facename, "west") == 0) {
                face_index = 4;
              } else if (strcmp(facename, "east") == 0) {
                face_index = 5;
              }

              json_object_element_s* face_element = json_value_as_object(face_obj_element->value)->start;
              RenderableFace* face = result.elements[result.element_count].faces + face_index;

              face->uv_from = Vector2f(0, 0);
              face->uv_to = Vector2f(1, 1);
              face->render = true;
              face->tintindex = 0xFFFF;

              while (face_element) {
                const char* face_property = face_element->name->string;

                if (strcmp(face_property, "texture") == 0) {
                  json_string_s* texture_str = json_value_as_string(face_element->value);
                  std::string texture_name(texture_str->string, texture_str->string_size);

                  while (texture_name[0] == '#') {
                    auto iter = texture_map.find(texture_name.c_str() + 1);
                    if (iter != texture_map.end()) {
                      texture_name = iter->second;
                    } else {
                      fprintf(stderr, "Failed to read texture %.*s\n", (u32)texture_str->string_size,
                              texture_str->string);
                      return result;
                    }
                  }

                  size_t prefix_size = 6;

                  if (texture_name.find(":") != std::string::npos) {
                    prefix_size = 16;
                  }

                  auto iter = texture_ids.find(texture_name.substr(prefix_size) + ".png");

                  if (iter != texture_ids.end()) {
                    face->texture_id = iter->second;
                  } else {
                    face->texture_id = 0;
                  }
                } else if (strcmp(face_property, "uv") == 0) {
                  Vector2f uv_from;
                  Vector2f uv_to;

                  json_array_element_s* value = json_value_as_array(face_element->value)->start;

                  uv_from[0] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;
                  value = value->next;
                  uv_from[1] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;
                  value = value->next;
                  uv_to[0] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;
                  value = value->next;
                  uv_to[1] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;

                  face->uv_from = uv_from;
                  face->uv_to = uv_to;
                } else if (strcmp(face_property, "tintindex") == 0) {
                  face->tintindex = (u32)strtol(json_value_as_number(face_element->value)->number, nullptr, 10);
                  if (strstr(path, "leaves") != 0) {
                    face->tintindex = 1;
                  }
                }

                face_element = face_element->next;
              }

              face_obj_element = face_obj_element->next;
            }
          }

          element_property = element_property->next;
        }

        ++result.element_count;
        assert(result.element_count < polymer_array_count(result.elements));

        element_array_element = element_array_element->next;
      }
    }

    root_element = root_element->next;
  }

  // Not sure how elements work yet. glazed terracotta has a parent that uses #all but it already has elements using
  // #pattern without setting #all.
  root_element = root_obj->start;
  while (root_element) {
    if (strcmp(root_element->name->string, "parent") == 0) {
      size_t prefix_size = 6;

      json_string_s* parent_name = json_value_as_string(root_element->value);

      for (size_t i = 0; i < parent_name->string_size; ++i) {
        if (parent_name->string[i] == ':') {
          prefix_size = 16;
          break;
        }
      }

      BlockModel parent =
          LoadModel(arena, zip, parent_name->string + prefix_size, parent_name->string_size, texture_map, texture_ids);
      for (size_t i = 0; i < parent.element_count; ++i) {
        result.elements[result.element_count++] = parent.elements[i];

        assert(result.element_count < polymer_array_count(result.elements));
      }
    }

    root_element = root_element->next;
  }

  for (size_t i = 0; i < result.element_count; ++i) {
    BlockElement* element = result.elements + i;

    element->occluding = element->from == Vector3f(0, 0, 0) && element->to == Vector3f(1, 1, 1);
  }

  return result;
}

bool GameState::LoadBlocks() {
  // TODO:
  // Read in all of the models in the jar
  // Read in blocks.json
  //  Serialize the properties into the same format as the jar's blockstates

  // For each state id in blocks.json, match it to a blockstate from the jar
  //  Read in each blockstate from the jar
  //    Read the model name for the block variant and match it to the model

  block_state_count = 0;

  FILE* f = fopen("blocks.json", "r");
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(trans_arena, char, file_size);

  fread(buffer, 1, file_size, f);
  fclose(f);

  json_value_s* root = json_parse(buffer, file_size);
  assert(root->type == json_type_object);

  json_object_s* root_obj = json_value_as_object(root);
  assert(root_obj);

  // Create a list of pointers to property strings stored in the transient arena
  char** properties = (char**)trans_arena->Allocate(sizeof(char*) * 32768);

  // Transient arena is used as a push buffer of property strings through parsing

  std::unordered_map<std::string, u32> default_ids;

  json_object_element_s* element = root_obj->start;
  while (element) {
    json_object_s* block_obj = json_value_as_object(element->value);
    assert(block_obj);

    char* block_name = block_names[block_name_count++];
    memcpy(block_name, element->name->string, element->name->string_size);

    json_object_element_s* block_element = block_obj->start;
    while (block_element) {
      if (strncmp(block_element->name->string, "states", block_element->name->string_size) == 0) {
        json_array_s* states = json_value_as_array(block_element->value);
        json_array_element_s* state_array_element = states->start;

        while (state_array_element) {
          json_object_s* state_obj = json_value_as_object(state_array_element->value);

          properties[block_state_count] = nullptr;

          json_object_element_s* state_element = state_obj->start;

          u32 id = 0;
          size_t index = 0;

          while (state_element) {
            if (strncmp(state_element->name->string, "id", state_element->name->string_size) == 0) {
              block_states[block_state_count].name = block_name;

              long block_id = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              block_states[block_state_count].id = block_id;

              id = (u32)block_id;
              index = block_state_count;

              ++block_state_count;
            }
            state_element = state_element->next;
          }

          state_element = state_obj->start;
          while (state_element) {
            if (strncmp(state_element->name->string, "default", state_element->name->string_size) == 0) {
              std::string str(block_name);
              default_ids[str] = id;
            } else if (strncmp(state_element->name->string, "properties", state_element->name->string_size) == 0) {
              // Loop over each property and create a single string that matches the format of blockstates in the jar
              json_object_s* property_object = json_value_as_object(state_element->value);
              json_object_element_s* property_element = property_object->start;

              // Realign the arena for the property pointer to be 32-bit aligned.
              char* property = (char*)trans_arena->Allocate(0, 4);
              properties[index] = property;
              size_t property_length = 0;

              while (property_element) {
                json_string_s* property_value = json_value_as_string(property_element->value);

                if (strcmp(property_element->name->string, "waterlogged") == 0) {
                  property_element = property_element->next;
                  continue;
                }

                // Allocate enough for property_name=property_value
                size_t alloc_size = property_element->name->string_size + 1 + property_value->string_size;

                property_length += alloc_size;

                char* p = (char*)trans_arena->Allocate(alloc_size, 1);

                // Allocate space for a comma to separate the properties
                if (property_element != property_object->start) {
                  trans_arena->Allocate(1, 1);
                  p[0] = ',';
                  ++p;
                  ++property_length;
                }

                memcpy(p, property_element->name->string, property_element->name->string_size);
                p[property_element->name->string_size] = '=';

                memcpy(p + property_element->name->string_size + 1, property_value->string,
                       property_value->string_size);

                property_element = property_element->next;
              }

              trans_arena->Allocate(1, 1);
              properties[index][property_length] = 0;
            }
            state_element = state_element->next;
          }

          state_array_element = state_array_element->next;
        }
      }

      block_element = block_element->next;
    }

    element = element->next;
  }

  free(root);

  ZipArchive zip;
  if (!zip.Open("1.16.4.jar")) {
    fprintf(stderr, "Requires 1.16.4.jar.\n");
    return false;
  }

  std::unordered_map<std::string, u32> texture_ids;

  size_t texture_count = 0;
  ZipArchiveElement* texture_files = zip.ListFiles(trans_arena, "assets/minecraft/textures/block/", &texture_count);
  renderer->CreateTexture(16, 16, texture_count);
  renderer->CreateDescriptorSetLayout();

  for (size_t i = 0; i < texture_count; ++i) {
    size_t size = 0;
    u8* raw_image = (u8*)zip.ReadFile(trans_arena, texture_files[i].name, &size);
    int width, height, channels;

    std::string texture_name(texture_files[i].name + 32);
    texture_ids[texture_name] = (u32)i;

    stbi_uc* image = stbi_load_from_memory(raw_image, (int)size, &width, &height, &channels, STBI_rgb_alpha);

    size_t texture_size = 16 * 16 * 4;
    renderer->PushTexture(image, texture_size, i);

    stbi_image_free(image);
  }

  size_t state_count;
  ZipArchiveElement* files = zip.ListFiles(trans_arena, "assets/minecraft/blockstates/", &state_count);

  // Loop through each blockstate asset and match the variant properties to the blocks.json list
  // TODO: Some of this could be sped up with hash maps, but not really necessary for now.
  // Alternatively, this data could all be loaded once, associated, and written off to a new asset file for faster loads
  for (size_t i = 0; i < state_count; ++i) {
    u8* arena_snapshot = trans_arena->current;
    size_t size;
    char* data = zip.ReadFile(trans_arena, files[i].name, &size);

    // Temporarily cut off the .json from the file name so the blockstate name is easily compared against
    files[i].name[strlen(files[i].name) - 5] = 0;
    // Amount of characters to skip over to get to the blockstate asset name
    constexpr size_t kBlockStateAssetSkip = 29;
    char* file_blockstate_name = files[i].name + kBlockStateAssetSkip;

    assert(data);

    json_value_s* root = json_parse(data, size);
    assert(root->type == json_type_object);

    json_object_s* root_obj = json_value_as_object(root);
    assert(root_obj);

    json_object_element_s* root_element = root_obj->start;

    while (root_element) {
      if (strncmp("variants", root_element->name->string, root_element->name->string_size) == 0) {
        json_object_s* variant_obj = json_value_as_object(root_element->value);
        json_object_element_s* variant_element = variant_obj->start;

        // Go through each block.json state and find any blockstates that match this filename
        for (size_t bid = 0; bid < block_state_count; ++bid) {
          char* file_name = files[i].name;
          char* bid_name = block_states[bid].name + 10;

          if (block_states[bid].model.element_count != 0) {
            continue;
          }

          if (strcmp(bid_name, file_blockstate_name) != 0) {
            continue;
          }

          json_object_element_s* variant_element = variant_obj->start;

          while (variant_element) {
            const char* variant_name = variant_element->name->string;

            if ((variant_element->name->string_size == 0 && properties[bid] == nullptr) ||
                (properties[bid] != nullptr && strcmp(variant_name, properties[bid]) == 0) || variant_element->next == nullptr) {
              json_object_s* state_details = nullptr;

              if (variant_element->value->type == json_type_array) {
                // TODO: Find out why multiple models are listed under one variant type. Just default to first for now.
                state_details = json_value_as_object(json_value_as_array(variant_element->value)->start->value);
              } else {
                state_details = json_value_as_object(variant_element->value);
              }

              json_object_element_s* state_element = state_details->start;

              while (state_element) {
                if (strcmp(state_element->name->string, "model") == 0) {
                  json_string_s* model_name_str = json_value_as_string(state_element->value);

                  // Do a lookup on the model name then store the model in the BlockState.
                  // Model lookup is going to need to be recursive with the root parent data being filled out first then
                  // cascaded down.
                  const size_t kPrefixSize = 16;

                  std::unordered_map<std::string, std::string> texture_map;

                  BlockModel model = LoadModel(trans_arena, zip, model_name_str->string + kPrefixSize,
                                               model_name_str->string_size - kPrefixSize, texture_map, texture_ids);

                  block_states[bid].model = model;
                  variant_element = nullptr;
                  break;
                }
                state_element = state_element->next;
              }
            }

            if (variant_element) {
              variant_element = variant_element->next;
            }
          }
        }

#if 0

        while (variant_element) {
          size_t block_id = -1;
          const char* variant_name = variant_element->name->string;

          for (size_t bid = 0; bid < block_state_count; ++bid) {
            char* file_name = files[i].name;
            char* bid_name = block_states[bid].name + 10;

            if (strcmp(bid_name, file_blockstate_name) != 0) {
              continue;
            }

            if (strcmp(bid_name, file_blockstate_name) == 0 &&
                ((variant_element->name->string_size == 0 && properties[bid] == nullptr) ||
                 (properties[bid] != nullptr && strcmp(variant_name, properties[bid]) == 0))) {
              block_id = bid;
              break;
            }
          }

          if (block_id == -1) {
            auto iter = default_ids.find("minecraft:" + std::string(file_blockstate_name));
            if (iter != default_ids.end()) {
              block_id = iter->second;
            }
          }

          if (block_id != -1) {
            json_object_s* state_details = nullptr;

            if (variant_element->value->type == json_type_array) {
              // TODO: Find out why multiple models are listed under one variant type. Just default to first for now.
              state_details = json_value_as_object(json_value_as_array(variant_element->value)->start->value);
            } else {
              state_details = json_value_as_object(variant_element->value);
            }

            json_object_element_s* state_element = state_details->start;

            while (state_element) {
              if (strcmp(state_element->name->string, "model") == 0) {
                json_string_s* model_name_str = json_value_as_string(state_element->value);

                // Do a lookup on the model name then store the model in the BlockState.
                // Model lookup is going to need to be recursive with the root parent data being filled out first then
                // cascaded down.
                const size_t kPrefixSize = 16;

                std::unordered_map<std::string, std::string> texture_map;

                BlockModel model = LoadModel(trans_arena, zip, model_name_str->string + kPrefixSize,
                                             model_name_str->string_size - kPrefixSize, texture_map, texture_ids);

                block_states[block_id].model = model;
              }
              state_element = state_element->next;
            }
          }

          variant_element = variant_element->next;
        }

#endif
      }
      root_element = root_element->next;
    }

    // Restore the .json to filename
    files[i].name[strlen(files[i].name) - 5] = '.';
    free(root);

    // Pop the current file off the stack allocator
    trans_arena->current = arena_snapshot;
  }

  zip.Close();

  return true;
}

} // namespace polymer
