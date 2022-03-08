#include "block_mesher.h"

#include "../block.h"

namespace polymer {
namespace render {

u32* CreateBorderedChunk(MemoryArena& arena, ChunkBuildContext* ctx, s32 chunk_y);

inline void PushVertex(MemoryArena& arena, render::ChunkVertex* vertices, u32* count, const Vector3f& position,
                       const Vector2f& uv, u32 texture_id, u32 tintindex, u32 ao) {
  arena.Allocate(sizeof(render::ChunkVertex), 1);

  vertices[*count].position = position;
  vertices[*count].texcoord = uv;
  vertices[*count].texture_id = texture_id;
  vertices[*count].tint_index = tintindex | (ao << 16);

  ++*count;
}

inline bool IsOccluding(BlockModel* from, BlockModel* to, BlockFace face) {
  // Only check the first element for transparency so blocks with overlay aren't treated as transparent.
  for (size_t j = 0; j < 6; ++j) {
    if (to->elements[0].faces[j].transparency) {
      return false;
    }
  }

  // TODO: Implement the rest
  return to->IsOccluding();
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

ChunkMesh BlockMesher::CreateMesh(BlockRegistry& block_registry, ChunkBuildContext* ctx, s32 chunk_y) {
  ChunkMesh mesh;

  s32 chunk_x = ctx->chunk_x;
  s32 chunk_z = ctx->chunk_z;

  u32* bordered_chunk = CreateBorderedChunk(arena, ctx, chunk_y);
  if (!bordered_chunk) return mesh;

  // Create an initial pointer to transient memory with zero vertices allocated.
  // Each push will allocate a new vertex with just a stack pointer increase so it's quick and contiguous.
  render::ChunkVertex* vertices = (render::ChunkVertex*)arena.Allocate(0);
  u32 vertex_count = 0;

  Vector3f chunk_base(chunk_x * 16.0f, chunk_y * 16.0f - 64.0f, chunk_z * 16.0f);

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

        if (!IsOccluding(model, above_model, BlockFace::Up)) {
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

            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);

            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);
            PushVertex(arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex, ele_ao_tl);
            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!IsOccluding(model, below_model, BlockFace::Down)) {
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

            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);

            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);
            PushVertex(arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex, ele_ao_tl);
            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!IsOccluding(model, north_model, BlockFace::North)) {
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

            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);

            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);
            PushVertex(arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex, ele_ao_tl);
            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!IsOccluding(model, south_model, BlockFace::South)) {
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

            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);

            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);
            PushVertex(arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex, ele_ao_tl);
            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!IsOccluding(model, east_model, BlockFace::East)) {
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

            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);

            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);
            PushVertex(arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex, ele_ao_tl);
            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }

        if (!IsOccluding(model, west_model, BlockFace::West)) {
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

            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
            PushVertex(arena, vertices, &vertex_count, bottom_right + chunk_base, br_uv, texture_id, tintindex,
                       ele_ao_br);
            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);

            PushVertex(arena, vertices, &vertex_count, top_right + chunk_base, tr_uv, texture_id, tintindex, ele_ao_tr);
            PushVertex(arena, vertices, &vertex_count, top_left + chunk_base, tl_uv, texture_id, tintindex, ele_ao_tl);
            PushVertex(arena, vertices, &vertex_count, bottom_left + chunk_base, bl_uv, texture_id, tintindex,
                       ele_ao_bl);
          }
        }
      }
    }
  }

  mesh.vertices = (u8*)vertices;
  mesh.vertex_count = vertex_count;

  return mesh;
}

u32* CreateBorderedChunk(MemoryArena& arena, ChunkBuildContext* ctx, s32 chunk_y) {
  u32* bordered_chunk = (u32*)arena.Allocate(sizeof(u32) * 18 * 18 * 18);

  memset(bordered_chunk, 0, sizeof(u32) * 18 * 18 * 18);

  ChunkSection* section = ctx->section;
  ChunkSection* east_section = ctx->east_section;
  ChunkSection* west_section = ctx->west_section;
  ChunkSection* north_section = ctx->north_section;
  ChunkSection* south_section = ctx->south_section;
  ChunkSection* north_east_section = ctx->north_east_section;
  ChunkSection* north_west_section = ctx->north_west_section;
  ChunkSection* south_east_section = ctx->south_east_section;
  ChunkSection* south_west_section = ctx->south_west_section;

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

  // South-east corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + 17);
    bordered_chunk[index] = south_east_section->chunks[chunk_y].blocks[y][0][0];
  }

  // North-east corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 0 * 18 + 17);
    bordered_chunk[index] = north_east_section->chunks[chunk_y].blocks[y][15][0];
  }

  // South-west corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + 0);
    bordered_chunk[index] = south_west_section->chunks[chunk_y].blocks[y][0][15];
  }

  // North-west corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 0 * 18 + 0);
    bordered_chunk[index] = north_west_section->chunks[chunk_y].blocks[y][15][15];
  }

  if (chunk_y < 16) {
    if (section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above blocks
      for (s64 z = 0; z < 16; ++z) {
        for (s64 x = 0; x < 16; ++x) {
          size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + (x + 1));
          bordered_chunk[index] = section->chunks[chunk_y + 1].blocks[0][z][x];
        }
      }
    }

    if (south_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-south
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(17 * 18 * 18 + 17 * 18 + (x + 1));
        bordered_chunk[index] = south_section->chunks[chunk_y + 1].blocks[0][0][x];
      }
    }

    if (north_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-north
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(17 * 18 * 18 + 0 * 18 + (x + 1));
        bordered_chunk[index] = north_section->chunks[chunk_y + 1].blocks[0][15][x];
      }
    }

    if (east_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-east
      for (s64 z = 0; z < 16; ++z) {
        size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + 17);
        bordered_chunk[index] = east_section->chunks[chunk_y + 1].blocks[0][z][0];
      }
    }

    if (west_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-west
      for (s64 z = 0; z < 16; ++z) {
        size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + 0);
        bordered_chunk[index] = west_section->chunks[chunk_y + 1].blocks[0][z][15];
      }
    }

    if (south_east_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-south-east
      bordered_chunk[(size_t)(17 * 18 * 18 + 17 * 18 + 17)] = south_east_section->chunks[chunk_y + 1].blocks[0][0][0];
    }

    if (south_west_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-south-west
      bordered_chunk[(size_t)(17 * 18 * 18 + 17 * 18 + 0)] = south_west_section->chunks[chunk_y + 1].blocks[0][0][15];
    }

    if (north_east_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-north-east
      bordered_chunk[(size_t)(17 * 18 * 18 + 0 * 18 + 17)] = north_east_section->chunks[chunk_y + 1].blocks[0][15][0];
    }

    if (north_west_section->info->bitmask & (1 << (chunk_y + 1))) {
      // Load above-north-west
      bordered_chunk[(size_t)(17 * 18 * 18 + 0 * 18 + 0)] = north_west_section->chunks[chunk_y + 1].blocks[0][15][15];
    }
  }

  if (chunk_y > 0) {
    if (section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below blocks
      for (s64 z = 0; z < 16; ++z) {
        for (s64 x = 0; x < 16; ++x) {
          size_t index = (size_t)((z + 1) * 18 + (x + 1));
          bordered_chunk[index] = section->chunks[chunk_y - 1].blocks[15][z][x];
        }
      }
    }

    if (south_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-south
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(0 * 18 * 18 + 17 * 18 + (x + 1));
        bordered_chunk[index] = south_section->chunks[chunk_y - 1].blocks[15][0][x];
      }
    }

    if (north_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-north
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(0 * 18 * 18 + 0 * 18 + (x + 1));
        bordered_chunk[index] = north_section->chunks[chunk_y - 1].blocks[15][15][x];
      }
    }

    if (east_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-east
      for (s64 z = 0; z < 16; ++z) {
        size_t index = (size_t)(0 * 18 * 18 + (z + 1) * 18 + 17);
        bordered_chunk[index] = east_section->chunks[chunk_y - 1].blocks[15][z][0];
      }
    }

    if (west_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-west
      for (s64 z = 0; z < 16; ++z) {
        size_t index = (size_t)(0 * 18 * 18 + (z + 1) * 18 + 0);
        bordered_chunk[index] = west_section->chunks[chunk_y - 1].blocks[15][z][15];
      }
    }

    if (south_east_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-south-east
      bordered_chunk[(size_t)(0 * 18 * 18 + 17 * 18 + 17)] = south_east_section->chunks[chunk_y - 1].blocks[15][0][0];
    }

    if (south_west_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-south-west
      bordered_chunk[(size_t)(0 * 18 * 18 + 17 * 18 + 0)] = south_west_section->chunks[chunk_y - 1].blocks[15][0][15];
    }

    if (north_east_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-north-east
      bordered_chunk[(size_t)(0 * 18 * 18 + 0 * 18 + 17)] = north_east_section->chunks[chunk_y - 1].blocks[15][15][0];
    }

    if (north_west_section->info->bitmask & (1 << (chunk_y - 1))) {
      // Load below-north-west
      bordered_chunk[(size_t)(0 * 18 * 18 + 0 * 18 + 0)] = north_west_section->chunks[chunk_y - 1].blocks[15][15][15];
    }
  }

  return bordered_chunk;
}

} // namespace render
} // namespace polymer
