#include "block_mesher.h"

#include "../world/block.h"

#include <assert.h>
#include <stdio.h>

using polymer::world::BlockElement;
using polymer::world::BlockFace;
using polymer::world::BlockModel;
using polymer::world::BlockRegistry;
using polymer::world::BlockState;
using polymer::world::BlockStateInfo;
using polymer::world::ChunkSection;
using polymer::world::kChunkColumnCount;
using polymer::world::RenderableFace;

namespace polymer {
namespace render {

u32* CreateBorderedChunk(MemoryArena& arena, ChunkBuildContext* ctx, s32 chunk_y);

struct LayerData {
  render::ChunkVertex* vertices;
  u32* count;
};

struct PushContext {
  MemoryArena* vertex_arenas[kRenderLayerCount];
  MemoryArena* index_arenas[kRenderLayerCount];
  bool anim_repeat;

  PushContext(bool anim_repeat) : anim_repeat(anim_repeat) {}

  void SetLayerData(RenderLayer layer, MemoryArena* vertex_arena, MemoryArena* index_arena) {
    vertex_arenas[(size_t)layer] = vertex_arena;
    index_arenas[(size_t)layer] = index_arena;
  }
};

inline u16 PushVertex(PushContext& ctx, const Vector3f& position, const Vector2f& uv, RenderableFace* face, u32 ao) {
  render::ChunkVertex* vertex =
      (render::ChunkVertex*)ctx.vertex_arenas[face->render_layer]->Allocate(sizeof(render::ChunkVertex), 1);

  vertex->position = position;

  u16 uv_x = (u16)(uv.x * 16);
  u16 uv_y = (u16)(uv.y * 16);

  vertex->packed_uv = (uv_x << 5) | (uv_y & 0x1F);

  vertex->texture_id = face->texture_id;

  u32 anim_count = face->frame_count;
  anim_count |= ((u32)ctx.anim_repeat << 7);

  vertex->tint_index = (face->tintindex & 0xFF) | ((anim_count & 0xFF) << 8) | (ao << 16);

  size_t index = (vertex - (render::ChunkVertex*)ctx.vertex_arenas[face->render_layer]->base);
  assert(index <= 65535);
  return (u16)index;
}

inline void PushIndex(PushContext& ctx, u32 render_layer, u16 index) {
  u16* out = (u16*)ctx.index_arenas[render_layer]->Allocate(sizeof(index), 1);
  *out = index;
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

struct MaterialDescription {
  bool fluid;
  bool water;
};

inline MaterialDescription GetMaterialDescription(BlockMesherMapping& mapping, u32 bid) {
  MaterialDescription result = {};

  result.water = mapping.water_range.Contains(bid) || mapping.kelp_range.Contains(bid) ||
                 mapping.seagrass_range.Contains(bid) || mapping.tall_seagrass_range.Contains(bid);

  result.fluid = result.water || mapping.lava_range.Contains(bid);

  return result;
}

inline u32 xorshift(u32 seed) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

static void RandomizeFaceTexture(u32 world_x, u32 world_y, u32 world_z, Vector2f& bl_uv, Vector2f& br_uv,
                                 Vector2f& tr_uv, Vector2f& tl_uv) {
  // TODO: Do this better. This is just some simple randomness
  u32 xr = xorshift(world_x * 3917 + world_y * 3701 + world_z * 181) % 16;
  u32 yr = xorshift(world_x * 1917 + world_y * 1559 + world_z * 381) % 16;
  u32 zr = xorshift(world_x * 10191 + world_y * 1319 + world_z * 831) % 16;
  u32 perm = xorshift(world_x * 171 + world_y * 7001 + world_z * 131) % 2;

  float du = (xr ^ yr) / 16.0f;
  float dv = (zr ^ yr) / 16.0f;

  Vector2f delta(du, dv);

  bl_uv += delta;
  br_uv += delta;
  tr_uv += delta;
  tl_uv += delta;

  switch (perm) {
  case 0: {
    // Flip horizontal
    Vector2f bt = br_uv;
    Vector2f tt = tr_uv;

    br_uv = bl_uv;
    bl_uv = bt;

    tr_uv = tl_uv;
    tl_uv = tt;
  } break;
  case 1: {
    // Flip vertical
    Vector2f rt = tr_uv;
    Vector2f lt = tl_uv;

    tr_uv = br_uv;
    br_uv = rt;

    tl_uv = bl_uv;
    bl_uv = lt;
  } break;
  }
}

static void MeshBlock(BlockMesher& mesher, PushContext& context, BlockRegistry& block_registry, u32* bordered_chunk,
                      u32 bid, size_t relative_x, size_t relative_y, size_t relative_z, const Vector3f& chunk_base) {
  BlockModel* model = &block_registry.states[bid].model;

  if (model->element_count == 0) {
    return;
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

      if (face->random_flip) {
        u32 world_x = (u32)(chunk_base.x + x);
        u32 world_y = (u32)(chunk_base.y + y);
        u32 world_z = (u32)(chunk_base.z + z);

        RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
      }

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

      u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
      u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
      u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
      u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

      PushIndex(context, face->render_layer, bli);
      PushIndex(context, face->render_layer, bri);
      PushIndex(context, face->render_layer, tri);

      PushIndex(context, face->render_layer, tri);
      PushIndex(context, face->render_layer, tli);
      PushIndex(context, face->render_layer, bli);
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

      if (face->random_flip) {
        u32 world_x = (u32)(chunk_base.x + x);
        u32 world_y = (u32)(chunk_base.y + y);
        u32 world_z = (u32)(chunk_base.z + z);

        RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
      }

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

      u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
      u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
      u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
      u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

      PushIndex(context, face->render_layer, bli);
      PushIndex(context, face->render_layer, bri);
      PushIndex(context, face->render_layer, tri);

      PushIndex(context, face->render_layer, tri);
      PushIndex(context, face->render_layer, tli);
      PushIndex(context, face->render_layer, bli);
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

      if (face->random_flip) {
        u32 world_x = (u32)(chunk_base.x + x);
        u32 world_y = (u32)(chunk_base.y + y);
        u32 world_z = (u32)(chunk_base.z + z);

        RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
      }

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

      u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
      u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
      u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
      u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

      PushIndex(context, face->render_layer, bli);
      PushIndex(context, face->render_layer, bri);
      PushIndex(context, face->render_layer, tri);

      PushIndex(context, face->render_layer, tri);
      PushIndex(context, face->render_layer, tli);
      PushIndex(context, face->render_layer, bli);
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

      if (face->random_flip) {
        u32 world_x = (u32)(chunk_base.x + x);
        u32 world_y = (u32)(chunk_base.y + y);
        u32 world_z = (u32)(chunk_base.z + z);

        RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
      }

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

      u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
      u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
      u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
      u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

      PushIndex(context, face->render_layer, bli);
      PushIndex(context, face->render_layer, bri);
      PushIndex(context, face->render_layer, tri);

      PushIndex(context, face->render_layer, tri);
      PushIndex(context, face->render_layer, tli);
      PushIndex(context, face->render_layer, bli);
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

      if (face->random_flip) {
        u32 world_x = (u32)(chunk_base.x + x);
        u32 world_y = (u32)(chunk_base.y + y);
        u32 world_z = (u32)(chunk_base.z + z);

        RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
      }

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

      u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
      u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
      u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
      u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

      PushIndex(context, face->render_layer, bli);
      PushIndex(context, face->render_layer, bri);
      PushIndex(context, face->render_layer, tri);

      PushIndex(context, face->render_layer, tri);
      PushIndex(context, face->render_layer, tli);
      PushIndex(context, face->render_layer, bli);
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

      if (face->random_flip) {
        u32 world_x = (u32)(chunk_base.x + x);
        u32 world_y = (u32)(chunk_base.y + y);
        u32 world_z = (u32)(chunk_base.z + z);

        RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
      }

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

      u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
      u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
      u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
      u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

      PushIndex(context, face->render_layer, bli);
      PushIndex(context, face->render_layer, bri);
      PushIndex(context, face->render_layer, tri);

      PushIndex(context, face->render_layer, tri);
      PushIndex(context, face->render_layer, tli);
      PushIndex(context, face->render_layer, bli);
    }
  }
}

// TODO: Real implementation.
static void MeshFluid(BlockMesher& mesher, PushContext& context, BlockRegistry& block_registry, u32* bordered_chunk,
                      u32 bid, size_t relative_x, size_t relative_y, size_t relative_z, const Vector3f& chunk_base,
                      asset::TextureIdRange texture_range, u32 tintindex, RenderLayer layer) {
  float x = (float)relative_x;
  float y = (float)relative_y;
  float z = (float)relative_z;
  Vector3f position = chunk_base + Vector3f(x, y, z);

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

  BlockModel* above_model = &block_registry.states[above_id].model;
  BlockModel* below_model = &block_registry.states[below_id].model;

  BlockModel* north_model = &block_registry.states[north_id].model;
  BlockModel* south_model = &block_registry.states[south_id].model;
  BlockModel* east_model = &block_registry.states[east_id].model;
  BlockModel* west_model = &block_registry.states[west_id].model;

  int ele_ao_bl = 3;
  int ele_ao_br = 3;
  int ele_ao_tl = 3;
  int ele_ao_tr = 3;

  Vector3f from(0, 0, 0);
  Vector3f to(1, 1, 1);

  RenderableFace face_;
  face_.uv_from = Vector2f(0, 0);
  face_.uv_to = Vector2f(1, 1);
  face_.frame_count = texture_range.count;
  face_.texture_id = texture_range.base;
  face_.tintindex = tintindex;
  face_.render_layer = (int)layer;
  RenderableFace* face = &face_;

  bool fluid_below = GetMaterialDescription(mesher.mapping, below_id).fluid;

  BlockMesherMapping& mapping = mesher.mapping;
  bool is_empty_above = above_id == 0 || mapping.lily_pad_range.Contains(above_id) ||
                        mapping.void_air_range.Contains(above_id) || mapping.cave_air_range.Contains(above_id);

  if (is_empty_above) {
    Vector3f to(1, 0.9f, 1);
    Vector3f bottom_left(x + from.x, y + to.y, z + from.z);
    Vector3f bottom_right(x + from.x, y + to.y, z + to.z);
    Vector3f top_left(x + to.x, y + to.y, z + from.z);
    Vector3f top_right(x + to.x, y + to.y, z + to.z);

    Vector2f bl_uv(face->uv_from.x, face->uv_from.y);
    Vector2f br_uv(face->uv_from.x, face->uv_to.y);
    Vector2f tr_uv(face->uv_to.x, face->uv_to.y);
    Vector2f tl_uv(face->uv_to.x, face->uv_from.y);

    u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
    u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
    u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
    u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tli);
  }

  if (below_id == 0) {
    Vector3f bottom_left(x + to.x, y + from.y, z + from.z);
    Vector3f bottom_right(x + to.x, y + from.y, z + to.z);
    Vector3f top_left(x + from.x, y + from.y, z + from.z);
    Vector3f top_right(x + from.x, y + from.y, z + to.z);

    Vector2f bl_uv(face->uv_to.x, face->uv_to.y);
    Vector2f br_uv(face->uv_to.x, face->uv_from.y);
    Vector2f tr_uv(face->uv_from.x, face->uv_from.y);
    Vector2f tl_uv(face->uv_from.x, face->uv_to.y);

    u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
    u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
    u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
    u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tli);
  }

  if (fluid_below) {
    from = Vector3f(0, -0.1f, 0);
  }

  to = Vector3f(1, 0.9f, 1);

  if (north_id == 0) {
    Vector3f bottom_left(x + to.x, y + from.y, z + from.z);
    Vector3f bottom_right(x + from.x, y + from.y, z + from.z);
    Vector3f top_left(x + to.x, y + to.y, z + from.z);
    Vector3f top_right(x + from.x, y + to.y, z + from.z);

    Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
    Vector2f br_uv(face->uv_to.x, face->uv_to.y);
    Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
    Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

    u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
    u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
    u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
    u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tli);
  }

  if (south_id == 0) {
    Vector3f bottom_left(x + from.x, y + from.y, z + to.z);
    Vector3f bottom_right(x + to.x, y + from.y, z + to.z);
    Vector3f top_left(x + from.x, y + to.y, z + to.z);
    Vector3f top_right(x + to.x, y + to.y, z + to.z);

    Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
    Vector2f br_uv(face->uv_to.x, face->uv_to.y);
    Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
    Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

    u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
    u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
    u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
    u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tli);
  }

  if (east_id == 0) {
    Vector3f bottom_left(x + to.x, y + from.y, z + to.z);
    Vector3f bottom_right(x + to.x, y + from.y, z + from.z);
    Vector3f top_left(x + to.x, y + to.y, z + to.z);
    Vector3f top_right(x + to.x, y + to.y, z + from.z);

    Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
    Vector2f br_uv(face->uv_to.x, face->uv_to.y);
    Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
    Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

    u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
    u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
    u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
    u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tli);
  }

  if (west_id == 0) {
    Vector3f bottom_left(x + from.x, y + from.y, z + from.z);
    Vector3f bottom_right(x + from.x, y + from.y, z + to.z);
    Vector3f top_left(x + from.x, y + to.y, z + from.z);
    Vector3f top_right(x + from.x, y + to.y, z + to.z);

    Vector2f bl_uv(face->uv_from.x, face->uv_to.y);
    Vector2f br_uv(face->uv_to.x, face->uv_to.y);
    Vector2f tr_uv(face->uv_to.x, face->uv_from.y);
    Vector2f tl_uv(face->uv_from.x, face->uv_from.y);

    u16 bli = PushVertex(context, bottom_left + chunk_base, bl_uv, face, ele_ao_bl);
    u16 bri = PushVertex(context, bottom_right + chunk_base, br_uv, face, ele_ao_br);
    u16 tri = PushVertex(context, top_right + chunk_base, tr_uv, face, ele_ao_tr);
    u16 tli = PushVertex(context, top_left + chunk_base, tl_uv, face, ele_ao_tl);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, tli);
  }
}

ChunkVertexData BlockMesher::CreateMesh(asset::AssetSystem& assets, BlockRegistry& block_registry,
                                        ChunkBuildContext* ctx, s32 chunk_y) {
  ChunkVertexData vertex_data;

  s32 chunk_x = ctx->chunk_x;
  s32 chunk_z = ctx->chunk_z;

  u32* bordered_chunk = CreateBorderedChunk(trans_arena, ctx, chunk_y);
  if (!bordered_chunk) return vertex_data;

  water_texture = assets.GetTextureRange(POLY_STR("assets/minecraft/textures/block/water_still.png"));
  asset::TextureIdRange lava_texture =
      assets.GetTextureRange(POLY_STR("assets/minecraft/textures/block/lava_still.png"));

  Vector3f chunk_base(chunk_x * 16.0f, chunk_y * 16.0f - 64.0f, chunk_z * 16.0f);

  PushContext context(false);

  context.SetLayerData(RenderLayer::Standard, &vertex_arenas[0], &index_arenas[0]);
  context.SetLayerData(RenderLayer::Flora, &vertex_arenas[1], &index_arenas[1]);
  context.SetLayerData(RenderLayer::Alpha, &vertex_arenas[2], &index_arenas[2]);

  for (size_t relative_y = 0; relative_y < 16; ++relative_y) {
    for (size_t relative_z = 0; relative_z < 16; ++relative_z) {
      for (size_t relative_x = 0; relative_x < 16; ++relative_x) {
        size_t index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);

        u32 bid = bordered_chunk[index];

        MaterialDescription desc = GetMaterialDescription(mapping, bid);

        if (desc.fluid) {
          RenderLayer layer = RenderLayer::Standard;
          asset::TextureIdRange texture_range = lava_texture;
          u32 tintindex = 0xFF;

          if (desc.water) {
            texture_range = water_texture;
            tintindex = 50;
            layer = RenderLayer::Alpha;
          }

          context.anim_repeat = true;
          MeshFluid(*this, context, block_registry, bordered_chunk, bid, relative_x, relative_y, relative_z, chunk_base,
                    texture_range, tintindex, layer);
        }

        context.anim_repeat = false;
        // Always mesh block even if it's a fluid because the plants have both
        MeshBlock(*this, context, block_registry, bordered_chunk, bid, relative_x, relative_y, relative_z, chunk_base);
      }
    }
  }

  for (size_t i = 0; i < kRenderLayerCount; ++i) {
    MemoryArena& vertex_arena = vertex_arenas[i];
    MemoryArena& index_arena = index_arenas[i];
    RenderLayer layer = (RenderLayer)i;

    u32 vertex_count = (u32)(((ptrdiff_t)vertex_arena.current - (ptrdiff_t)vertex_arena.base) / sizeof(ChunkVertex));
    u32 index_count = (u32)(((ptrdiff_t)index_arena.current - (ptrdiff_t)index_arena.base) / sizeof(u16));

    vertex_data.SetVertices(layer, vertex_arenas[i].base, vertex_count);
    vertex_data.SetIndices(layer, (u16*)index_arenas[i].base, index_count);
  }

  return vertex_data;
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

  if (chunk_y < kChunkColumnCount) {
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
