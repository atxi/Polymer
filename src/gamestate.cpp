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
                       const Vector2f& uv, u32 texture_id, u32 tintindex, u32 ao) {
  arena->Allocate(sizeof(ChunkVertex), 1);

  vertices[*count].position = position;
  vertices[*count].texcoord = uv;
  vertices[*count].texture_id = texture_id;
  vertices[*count].tint_index = tintindex | (ao << 16);

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

    // TODO: Put a cap on the distance away so it's not constantly grabbing the neighbors of the outer unmeshed chunks.
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

      Vector3f column_min(section_info->x * 16.0f, 0, section_info->z * 16.0f);
      Vector3f column_max(section_info->x * 16.0f + 16.0f, 16 * 16.0f, section_info->z * 16.0f + 16.0f);

      if (!frustum.Intersects(column_min, column_max)) {
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
  constexpr float kMaxPitch = Radians(89.0f);

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
  ChunkSection* section = &world.chunks[z_index][x_index];
  ChunkSectionInfo* section_info = &world.chunk_infos[z_index][x_index];

  section_info->loaded = false;

  for (size_t chunk_y = 0; chunk_y < 16; ++chunk_y) {
    if (section_info->bitmask & (1 << chunk_y)) {
      memset(section->chunks[chunk_y].blocks, 0, sizeof(section->chunks[chunk_y].blocks));
    }
  }

  section_info->bitmask = 0;

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

inline int GetAmbientOcclusion(BlockModel* side1, BlockModel* side2, BlockModel* corner) {
  int value1 = side1->IsOccluding();
  int value2 = side2->IsOccluding();
  int value_corner = corner->IsOccluding();

  if (value1 && value2) {
    return 0;
  }

  return 3 - (value1 + value2 + value_corner);
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
  if (west_section->info->bitmask & (1 << chunk_y)) {
    for (s64 y = 0; y < 16; ++y) {
      for (s64 z = 0; z < 16; ++z) {
        size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + 0);
        bordered_chunk[index] = west_section->chunks[chunk_y].blocks[y][z][15];
      }
    }
  }

  // Load east blocks
  if (east_section->info->bitmask & (1 << chunk_y)) {
    for (s64 y = 0; y < 16; ++y) {
      for (s64 z = 0; z < 16; ++z) {
        size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + 17);
        bordered_chunk[index] = east_section->chunks[chunk_y].blocks[y][z][0];
      }
    }
  }

  // Load north blocks
  if (north_section->info->bitmask & (1 << chunk_y)) {
    for (s64 y = 0; y < 16; ++y) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)((y + 1) * 18 * 18 + (x + 1));
        bordered_chunk[index] = north_section->chunks[chunk_y].blocks[y][15][x];
      }
    }
  }

  // Load south blocks
  if (south_section->info->bitmask & (1 << chunk_y)) {
    for (s64 y = 0; y < 16; ++y) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + (x + 1));
        bordered_chunk[index] = south_section->chunks[chunk_y].blocks[y][0][x];
      }
    }
  }

  if (chunk_y < 16 && section->info->bitmask & (1 << (chunk_y + 1))) {
    // Load above blocks
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + (x + 1));
        bordered_chunk[index] = section->chunks[chunk_y + 1].blocks[0][z][x];
      }
    }
  }

  if (chunk_y > 0 && section->info->bitmask & (1 << (chunk_y - 1))) {
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
        BlockModel* model = &block_registry.states[bid].model;

        if (model->element_count == 0) {
          continue;
        }

        size_t above_index = (relative_y + 2) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);
        size_t below_index = (relative_y + 0) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);

        size_t north_index = (relative_y + 1) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 1);
        size_t south_index = (relative_y + 1) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 1);
        size_t east_index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 2);
        size_t west_index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 0);

        size_t north_west_index = (relative_y + 1) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 0);
        size_t north_east_index = (relative_y + 1) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 2);
        size_t south_west_index = (relative_y + 1) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 0);
        size_t south_east_index = (relative_y + 1) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 2);

        size_t above_west_index = (relative_y + 2) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 0);
        size_t above_east_index = (relative_y + 2) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 2);
        size_t above_north_index = (relative_y + 2) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 1);
        size_t above_south_index = (relative_y + 2) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 1);

        size_t above_north_west_index = (relative_y + 2) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 0);
        size_t above_north_east_index = (relative_y + 2) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 2);
        size_t above_south_west_index = (relative_y + 2) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 0);
        size_t above_south_east_index = (relative_y + 2) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 2);

        size_t below_west_index = (relative_y + 0) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 0);
        size_t below_east_index = (relative_y + 0) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 2);
        size_t below_north_index = (relative_y + 0) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 1);
        size_t below_south_index = (relative_y + 0) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 1);

        size_t below_north_west_index = (relative_y + 0) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 0);
        size_t below_north_east_index = (relative_y + 0) * 18 * 18 + (relative_z + 0) * 18 + (relative_x + 2);
        size_t below_south_west_index = (relative_y + 0) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 0);
        size_t below_south_east_index = (relative_y + 0) * 18 * 18 + (relative_z + 2) * 18 + (relative_x + 2);

        u32 above_id = bordered_chunk[above_index];
        u32 below_id = bordered_chunk[below_index];
        u32 north_id = bordered_chunk[north_index];
        u32 south_id = bordered_chunk[south_index];
        u32 east_id = bordered_chunk[east_index];
        u32 west_id = bordered_chunk[west_index];

        float x = (float)relative_x;
        float y = (float)relative_y;
        float z = (float)relative_z;

        BlockModel* above_model = &block_registry.states[above_id].model;
        BlockModel* below_model = &block_registry.states[below_id].model;

        BlockModel* north_model = &block_registry.states[north_id].model;
        BlockModel* south_model = &block_registry.states[south_id].model;
        BlockModel* east_model = &block_registry.states[east_id].model;
        BlockModel* west_model = &block_registry.states[west_id].model;

        if (!above_model->IsOccluding()) {
          int ao_bl = 3;
          int ao_br = 3;
          int ao_tl = 3;
          int ao_tr = 3;

          if (model->HasShadedElement()) {
            BlockModel* above_west_model = &block_registry.states[bordered_chunk[above_west_index]].model;
            BlockModel* above_east_model = &block_registry.states[bordered_chunk[above_east_index]].model;
            BlockModel* above_north_model = &block_registry.states[bordered_chunk[above_north_index]].model;
            BlockModel* above_south_model = &block_registry.states[bordered_chunk[above_south_index]].model;
            BlockModel* above_north_west_model = &block_registry.states[bordered_chunk[above_north_west_index]].model;
            BlockModel* above_south_west_model = &block_registry.states[bordered_chunk[above_south_west_index]].model;
            BlockModel* above_north_east_model = &block_registry.states[bordered_chunk[above_north_east_index]].model;
            BlockModel* above_south_east_model = &block_registry.states[bordered_chunk[above_south_east_index]].model;

            ao_bl = GetAmbientOcclusion(above_west_model, above_north_model, above_north_west_model);
            ao_br = GetAmbientOcclusion(above_west_model, above_south_model, above_south_west_model);

            ao_tl = GetAmbientOcclusion(above_east_model, above_north_model, above_north_east_model);
            ao_tr = GetAmbientOcclusion(above_east_model, above_south_model, above_south_east_model);
          }

          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 1;

            if (!face->render)
              continue;

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

            int ele_ao_bl = 3;
            int ele_ao_br = 3;
            int ele_ao_tl = 3;
            int ele_ao_tr = 3;

            if (element->shade) {
              ele_ao_bl = ao_bl;
              ele_ao_br = ao_br;
              ele_ao_tl = ao_tl;
              ele_ao_tr = ao_tr;
            }

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex,
                       ele_ao_tl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!below_model->IsOccluding()) {
          int ao_bl = 3;
          int ao_br = 3;
          int ao_tl = 3;
          int ao_tr = 3;

          if (model->HasShadedElement()) {
            BlockModel* below_west_model = &block_registry.states[bordered_chunk[below_west_index]].model;
            BlockModel* below_east_model = &block_registry.states[bordered_chunk[below_east_index]].model;
            BlockModel* below_north_model = &block_registry.states[bordered_chunk[below_north_index]].model;
            BlockModel* below_south_model = &block_registry.states[bordered_chunk[below_south_index]].model;
            BlockModel* below_north_west_model = &block_registry.states[bordered_chunk[below_north_west_index]].model;
            BlockModel* below_south_west_model = &block_registry.states[bordered_chunk[below_south_west_index]].model;
            BlockModel* below_north_east_model = &block_registry.states[bordered_chunk[below_north_east_index]].model;
            BlockModel* below_south_east_model = &block_registry.states[bordered_chunk[below_south_east_index]].model;

            ao_bl = GetAmbientOcclusion(below_east_model, below_north_model, below_north_east_model);
            ao_br = GetAmbientOcclusion(below_east_model, below_south_model, below_south_east_model);

            ao_tl = GetAmbientOcclusion(below_west_model, below_north_model, below_north_west_model);
            ao_tr = GetAmbientOcclusion(below_west_model, below_south_model, below_south_west_model);
          }

          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 0;

            if (!face->render)
              continue;

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

            int ele_ao_bl = 3;
            int ele_ao_br = 3;
            int ele_ao_tl = 3;
            int ele_ao_tr = 3;

            if (element->shade) {
              ele_ao_bl = ao_bl;
              ele_ao_br = ao_br;
              ele_ao_tl = ao_tl;
              ele_ao_tr = ao_tr;
            }

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex,
                       ele_ao_tl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!north_model->IsOccluding()) {
          int ao_bl = 3;
          int ao_br = 3;
          int ao_tl = 3;
          int ao_tr = 3;

          if (model->HasShadedElement()) {
            BlockModel* north_east_model = &block_registry.states[bordered_chunk[north_east_index]].model;
            BlockModel* north_west_model = &block_registry.states[bordered_chunk[north_west_index]].model;
            BlockModel* above_north_model = &block_registry.states[bordered_chunk[above_north_index]].model;
            BlockModel* above_north_east_model = &block_registry.states[bordered_chunk[above_north_east_index]].model;
            BlockModel* above_north_west_model = &block_registry.states[bordered_chunk[above_north_west_index]].model;
            BlockModel* below_north_model = &block_registry.states[bordered_chunk[below_north_index]].model;
            BlockModel* below_north_east_model = &block_registry.states[bordered_chunk[below_north_east_index]].model;
            BlockModel* below_north_west_model = &block_registry.states[bordered_chunk[below_north_west_index]].model;

            ao_bl = GetAmbientOcclusion(north_east_model, below_north_model, below_north_east_model);
            ao_br = GetAmbientOcclusion(below_north_model, north_west_model, below_north_west_model);

            ao_tl = GetAmbientOcclusion(above_north_model, north_east_model, above_north_east_model);
            ao_tr = GetAmbientOcclusion(above_north_model, north_west_model, above_north_west_model);
          }

          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 2;

            if (!face->render)
              continue;

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

            int ele_ao_bl = 3;
            int ele_ao_br = 3;
            int ele_ao_tl = 3;
            int ele_ao_tr = 3;

            if (element->shade) {
              ele_ao_bl = ao_bl;
              ele_ao_br = ao_br;
              ele_ao_tl = ao_tl;
              ele_ao_tr = ao_tr;
            }

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex,
                       ele_ao_tl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!south_model->IsOccluding()) {
          int ao_bl = 3;
          int ao_br = 3;
          int ao_tl = 3;
          int ao_tr = 3;

          if (model->HasShadedElement()) {
            BlockModel* south_east_model = &block_registry.states[bordered_chunk[south_east_index]].model;
            BlockModel* south_west_model = &block_registry.states[bordered_chunk[south_west_index]].model;

            BlockModel* above_south_model = &block_registry.states[bordered_chunk[above_south_index]].model;
            BlockModel* above_south_east_model = &block_registry.states[bordered_chunk[above_south_east_index]].model;
            BlockModel* above_south_west_model = &block_registry.states[bordered_chunk[above_south_west_index]].model;

            BlockModel* below_south_model = &block_registry.states[bordered_chunk[below_south_index]].model;
            BlockModel* below_south_east_model = &block_registry.states[bordered_chunk[below_south_east_index]].model;
            BlockModel* below_south_west_model = &block_registry.states[bordered_chunk[below_south_west_index]].model;

            ao_bl = GetAmbientOcclusion(south_west_model, below_south_model, below_south_west_model);
            ao_br = GetAmbientOcclusion(south_east_model, below_south_model, below_south_east_model);

            ao_tl = GetAmbientOcclusion(above_south_model, south_west_model, above_south_west_model);
            ao_tr = GetAmbientOcclusion(above_south_model, south_east_model, above_south_east_model);
          }

          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 3;

            if (!face->render)
              continue;

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

            int ele_ao_bl = 3;
            int ele_ao_br = 3;
            int ele_ao_tl = 3;
            int ele_ao_tr = 3;

            if (element->shade) {
              ele_ao_bl = ao_bl;
              ele_ao_br = ao_br;
              ele_ao_tl = ao_tl;
              ele_ao_tr = ao_tr;
            }

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex,
                       ele_ao_tl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!east_model->IsOccluding()) {
          int ao_bl = 3;
          int ao_br = 3;
          int ao_tl = 3;
          int ao_tr = 3;

          if (model->HasShadedElement()) {
            BlockModel* north_east_model = &block_registry.states[bordered_chunk[north_east_index]].model;
            BlockModel* south_east_model = &block_registry.states[bordered_chunk[south_east_index]].model;
            BlockModel* above_east_model = &block_registry.states[bordered_chunk[above_east_index]].model;
            BlockModel* above_north_east_model = &block_registry.states[bordered_chunk[above_north_east_index]].model;
            BlockModel* above_south_east_model = &block_registry.states[bordered_chunk[above_south_east_index]].model;
            BlockModel* below_east_model = &block_registry.states[bordered_chunk[below_east_index]].model;
            BlockModel* below_north_east_model = &block_registry.states[bordered_chunk[below_north_east_index]].model;
            BlockModel* below_south_east_model = &block_registry.states[bordered_chunk[below_south_east_index]].model;

            ao_bl = GetAmbientOcclusion(south_east_model, below_east_model, below_south_east_model);
            ao_br = GetAmbientOcclusion(below_east_model, north_east_model, below_north_east_model);

            ao_tl = GetAmbientOcclusion(above_east_model, south_east_model, above_south_east_model);
            ao_tr = GetAmbientOcclusion(above_east_model, north_east_model, above_north_east_model);
          }

          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 5;

            if (!face->render)
              continue;

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

            int ele_ao_bl = 3;
            int ele_ao_br = 3;
            int ele_ao_tl = 3;
            int ele_ao_tr = 3;

            if (element->shade) {
              ele_ao_bl = ao_bl;
              ele_ao_br = ao_br;
              ele_ao_tl = ao_tl;
              ele_ao_tr = ao_tr;
            }

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex,
                       ele_ao_tl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!west_model->IsOccluding()) {
          int ao_bl = 3;
          int ao_br = 3;
          int ao_tl = 3;
          int ao_tr = 3;

          if (model->HasShadedElement()) {
            BlockModel* north_west_model = &block_registry.states[bordered_chunk[north_west_index]].model;
            BlockModel* south_west_model = &block_registry.states[bordered_chunk[south_west_index]].model;
            BlockModel* above_west_model = &block_registry.states[bordered_chunk[above_west_index]].model;
            BlockModel* above_north_west_model = &block_registry.states[bordered_chunk[above_north_west_index]].model;
            BlockModel* above_south_west_model = &block_registry.states[bordered_chunk[above_south_west_index]].model;
            BlockModel* below_west_model = &block_registry.states[bordered_chunk[below_west_index]].model;
            BlockModel* below_north_west_model = &block_registry.states[bordered_chunk[below_north_west_index]].model;
            BlockModel* below_south_west_model = &block_registry.states[bordered_chunk[below_south_west_index]].model;

            ao_bl = GetAmbientOcclusion(below_west_model, north_west_model, below_north_west_model);
            ao_br = GetAmbientOcclusion(below_west_model, south_west_model, below_south_west_model);

            ao_tl = GetAmbientOcclusion(above_west_model, north_west_model, above_north_west_model);
            ao_tr = GetAmbientOcclusion(above_west_model, south_west_model, above_south_west_model);
          }

          for (size_t i = 0; i < model->element_count; ++i) {
            BlockElement* element = model->elements + i;
            RenderableFace* face = element->faces + 4;

            if (!face->render)
              continue;

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

            int ele_ao_bl = 3;
            int ele_ao_br = 3;
            int ele_ao_tl = 3;
            int ele_ao_tr = 3;

            if (element->shade) {
              ele_ao_bl = ao_bl;
              ele_ao_br = ao_br;
              ele_ao_tl = ao_tl;
              ele_ao_tr = ao_tr;
            }

            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);

            PushVertex(trans_arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex,
                       ele_ao_tr);
            PushVertex(trans_arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex,
                       ele_ao_tl);
            PushVertex(trans_arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
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

  renderer->BeginMeshAllocation();

  for (s32 chunk_y = 0; chunk_y < 16; ++chunk_y) {
    if (!(section_info->bitmask & (1 << chunk_y))) {
      continue;
    }

    BuildChunkMesh(ctx, chunk_x, chunk_y, chunk_z);
  }

  renderer->EndMeshAllocation();
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
  section_info->x = chunk_x;
  section_info->z = chunk_z;

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

    // TODO: Block changes should be batched to update a chunk once in the frame when it changes
    renderer->BeginMeshAllocation();
    BuildChunkMesh(&ctx, chunk_x, chunk_y, chunk_z);
    renderer->EndMeshAllocation();
  }
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

} // namespace polymer
