#include <polymer/render/block_mesher.h>

#include <polymer/math.h>
#include <polymer/world/block.h>

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

struct BorderedChunk {
  constexpr static size_t kElementCount = 18 * 18 * 18;

  u32 blocks[kElementCount];

  // The bottom 4 bits contain the skylight data and the upper 4 bits contain the block
  u8 lightmap[kElementCount];

  // Position is chunk relative
  inline u8 GetBlockLight(size_t index) const {
    return lightmap[index] >> 4;
  }

  // Position is chunk relative
  inline u8 GetSkyLight(size_t index) const {
    return lightmap[index] & 0x0F;
  }
};

BorderedChunk* CreateBorderedChunk(MemoryArena& arena, ChunkBuildContext* ctx, s32 chunk_y);

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

inline u16 PushVertex(PushContext& ctx, const Vector3f& position, const Vector2f& uv, RenderableFace* face, u16 light,
                      bool shaded_axis = false) {
  render::ChunkVertex* vertex =
      (render::ChunkVertex*)ctx.vertex_arenas[face->render_layer]->Allocate(sizeof(render::ChunkVertex), 1);

  vertex->position = position;

  u16 uv_x = (u16)(uv.x * 16);
  u16 uv_y = (u16)(uv.y * 16);

  vertex->packed_uv = (uv_x << 5) | (uv_y & 0x1F);

  vertex->texture_id = face->texture_id;

  u8 packed_anim = (ctx.anim_repeat << 7) | (u8)face->frame_count;
  u8 tintindex = (u8)face->tintindex;
  light |= (shaded_axis << 15);

  vertex->packed_light = (packed_anim << 24) | (tintindex << 16) | light;

  size_t index = (vertex - (render::ChunkVertex*)ctx.vertex_arenas[face->render_layer]->base);
  assert(index <= 65535);
  return (u16)index;
}

inline void PushIndex(PushContext& ctx, u32 render_layer, u16 index) {
  u16* out = (u16*)ctx.index_arenas[render_layer]->Allocate(sizeof(index), 1);
  *out = index;
}

inline bool HasOccludableFace(BlockModel& model, BlockFace face) {
  for (size_t i = 0; i < model.element_count; ++i) {
    RenderableFace& render_face = model.elements[i].faces[(size_t)face];

    if (!render_face.render) continue;

    if (!render_face.transparency) {
      return true;
    }
  }

  return false;
}

inline bool IsOccluding(BlockModel* from, BlockModel* to, BlockFace face) {
  BlockFace opposite_face = world::GetOppositeFace(face);

  bool from_is_transparent = !HasOccludableFace(*from, face);
  bool to_is_transparent = !HasOccludableFace(*to, opposite_face);

  // TODO: Clean this up once rotation is settled.
  if (to->element_count == 0) return false;
  if (from->has_rotation || to->has_rotation || from->has_variant_rotation || to->has_variant_rotation) return false;
  if (to->has_leaves || !to->has_shaded) return false;

  for (size_t i = 0; i < from->element_count; ++i) {
    RenderableFace& from_face = from->elements[i].GetFace(face);

    if (!from_face.render || from->elements[i].rescale) continue;

    for (size_t j = 0; j < to->element_count; ++j) {
      RenderableFace& to_face = to->elements[i].GetFace(opposite_face);

      if (!to_face.render) continue;

      Vector3f& from_start = from->elements[i].from;
      Vector3f& from_end = from->elements[i].to;
      Vector3f& to_start = to->elements[i].from;
      Vector3f& to_end = to->elements[i].to;

      // Check if the element of the 'to' model fully occludes the 'from' face
      if (to_start.x <= from_start.x && to_start.y <= from_start.y && to_start.z <= from_start.z &&
          to_end.x >= from_end.x && to_end.y >= from_end.y && to_end.z >= from_end.z) {
        if (to_is_transparent) {
          if (from_is_transparent) {
            return true;
          }
          return false;
        }

        if (from_is_transparent) {
          if (to_is_transparent) {
            return true;
          }
          return false;
        }

        return true;
      }
    }
  }

  return false;
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

inline u32 CalculateVertexLight(BorderedChunk* bordered_chunk, size_t* indices, size_t current_index) {
  u32 sky_sum = 0;
  u32 block_sum = 0;

  for (size_t i = 0; i < 4; ++i) {
    u8 current_sky = bordered_chunk->GetSkyLight(indices[i]);
    u8 current_block = bordered_chunk->GetBlockLight(indices[i]);

    if (current_sky == 0) {
      current_sky = bordered_chunk->GetSkyLight(current_index);
    }

    if (current_block == 0) {
      current_block = bordered_chunk->GetBlockLight(current_index);
    }

    sky_sum += current_sky;
    block_sum += current_block;
  }

  return (block_sum << 6) | sky_sum;
}

struct FaceMesh {
  Vector3f bl_lookups[3];
  Vector3f br_lookups[3];
  Vector3f tl_lookups[3];
  Vector3f tr_lookups[3];

  Vector3f direction;

  Vector3f bl_pos;
  Vector3f br_pos;
  Vector3f tl_pos;
  Vector3f tr_pos;

  Vector2f bl_uv;
  Vector2f br_uv;
  Vector2f tl_uv;
  Vector2f tr_uv;

  bool uvlock;

  inline void PerformRotation(float angle, const Vector3f& axis, const Vector3f& origin) {
    bl_pos = Rotate(bl_pos - origin, angle, axis) + origin;
    br_pos = Rotate(br_pos - origin, angle, axis) + origin;
    tl_pos = Rotate(tl_pos - origin, angle, axis) + origin;
    tr_pos = Rotate(tr_pos - origin, angle, axis) + origin;

    this->direction = Rotate(this->direction, angle, axis);

    for (size_t i = 0; i < 3; ++i) {
      bl_lookups[i] = Rotate(bl_lookups[i], angle, axis);
      br_lookups[i] = Rotate(br_lookups[i], angle, axis);
      tl_lookups[i] = Rotate(tl_lookups[i], angle, axis);
      tr_lookups[i] = Rotate(tr_lookups[i], angle, axis);
    }
  }

  void RotateFace(const BlockModel& model, const BlockElement& element, const Vector3f& base) {
    Vector3f ele_axis = element.rotation.axis;
    Vector3f ele_origin = element.rotation.origin;

    // Rotate the elements by the variant rotation before rotating the actual elements
    if (model.has_variant_rotation) {
      Vector3f origin(0.5f, 0.5f, 0.5f);

      if (element.variant_rotation.x) {
        float angle = Radians((float)element.variant_rotation.x);
        Vector3f axis(1, 0, 0);

        PerformRotation(angle, axis, origin);

        ele_axis = Rotate(ele_axis, angle, axis);
        ele_origin = Rotate(ele_origin - origin, angle, axis) + origin;
      }

      if (element.variant_rotation.y) {
        float angle = -Radians((float)element.variant_rotation.y);
        Vector3f axis(0, 1, 0);

        PerformRotation(angle, axis, origin);

        ele_axis = Rotate(ele_axis, angle, axis);
        ele_origin = Rotate(ele_origin - origin, angle, axis) + origin;
      }

      if (element.variant_rotation.z) {
        float angle = Radians((float)element.variant_rotation.z);
        Vector3f axis(0, 0, 1);

        PerformRotation(angle, axis, origin);

        ele_axis = Rotate(ele_axis, angle, axis);
        ele_origin = Rotate(ele_origin - origin, angle, axis) + origin;
      }
    }

    if (element.rotation.angle != 0.0f) {
      float angle = Radians((float)element.rotation.angle);

      bl_pos = Rotate(bl_pos - ele_origin, angle, ele_axis) + ele_origin;
      br_pos = Rotate(br_pos - ele_origin, angle, ele_axis) + ele_origin;
      tl_pos = Rotate(tl_pos - ele_origin, angle, ele_axis) + ele_origin;
      tr_pos = Rotate(tr_pos - ele_origin, angle, ele_axis) + ele_origin;

      this->direction = Rotate(this->direction, angle, ele_axis);

      for (size_t i = 0; i < 3; ++i) {
        bl_lookups[i] = Rotate(bl_lookups[i], angle, ele_axis);
        br_lookups[i] = Rotate(br_lookups[i], angle, ele_axis);
        tl_lookups[i] = Rotate(tl_lookups[i], angle, ele_axis);
        tr_lookups[i] = Rotate(tr_lookups[i], angle, ele_axis);
      }
    }

    bl_pos += base;
    br_pos += base;
    tl_pos += base;
    tr_pos += base;
  }

  inline size_t GetIndex(Vector3f lookup) {
    int x = (int)floorf(lookup.x);
    int y = (int)floorf(lookup.y);
    int z = (int)floorf(lookup.z);

    if (x < -1) ++x;
    if (y < -1) ++y;
    if (z < -1) ++z;

    if (x > 17) --x;
    if (y > 17) --y;
    if (z > 17) --z;

    assert(x >= -1 && x <= 17);
    assert(y >= -1 && y <= 17);
    assert(z >= -1 && z <= 17);

    return (y + 1) * 18 * 18 + (z + 1) * 18 + (x + 1);
  }

  inline u32 CalculateVertexLight(BorderedChunk* bordered_chunk, const Vector3f& relative_pos, Vector3f* lookups) {
    size_t indices[4];

    indices[0] = GetIndex(relative_pos + Vector3f(0.5f, 0.5f, 0.5f) + this->direction);

    for (size_t i = 0; i < 3; ++i) {
      size_t index = GetIndex(relative_pos + lookups[i]);

      indices[i + 1] = index;
    }

    return ::polymer::render::CalculateVertexLight(bordered_chunk, indices, indices[0]);
  }

  inline u32 CalculateSharedLight(BorderedChunk* bordered_chunk, const Vector3f& relative_pos) {
    size_t current_index = GetIndex(relative_pos);
    u32 shared_skylight = bordered_chunk->GetSkyLight(current_index) * 4;
    u32 shared_blocklight = bordered_chunk->GetBlockLight(current_index) * 4;
    u32 shared_light = (shared_blocklight << 6) | shared_skylight;

    return shared_light;
  }

  inline int GetAmbientOcclusion(BlockRegistry& registry, BorderedChunk& bordered_chunk, const Vector3f& relative_pos,
                                 Vector3f* lookups) {
    BlockModel* models[3];

    for (size_t i = 0; i < 3; ++i) {
      size_t index = GetIndex(relative_pos + Vector3f(0.5f, 0.5f, 0.5f) + lookups[i]);
      u32 bid = bordered_chunk.blocks[index];
      models[i] = &registry.states[bid].model;
    }

    return GetAmbientOcclusion(models[0], models[1], models[2]);
  }

  inline int GetAmbientOcclusion(BlockModel* side1, BlockModel* side2, BlockModel* corner) {
    int value1 = side1->HasOccluding() && !side1->has_glass && !side1->has_variant_rotation;
    int value2 = side2->HasOccluding() && !side2->has_glass && !side2->has_variant_rotation;
    int value_corner = corner->HasOccluding() && !corner->has_glass && !corner->has_variant_rotation;

    if (value1 && value2) {
      return 0;
    }

    return 3 - (value1 + value2 + value_corner);
  }

  inline void SetPositions(const Vector3f& from, const Vector3f& to, BlockFace direction) {
    switch (direction) {
    case BlockFace::Down: {
      bl_pos = Vector3f(to.x, from.y, from.z);
      br_pos = Vector3f(to.x, from.y, to.z);
      tl_pos = Vector3f(from.x, from.y, from.z);
      tr_pos = Vector3f(from.x, from.y, to.z);
    } break;
    case BlockFace::Up: {
      bl_pos = Vector3f(from.x, to.y, from.z);
      br_pos = Vector3f(from.x, to.y, to.z);
      tl_pos = Vector3f(to.x, to.y, from.z);
      tr_pos = Vector3f(to.x, to.y, to.z);
    } break;
    case BlockFace::North: {
      bl_pos = Vector3f(to.x, from.y, from.z);
      br_pos = Vector3f(from.x, from.y, from.z);
      tl_pos = Vector3f(to.x, to.y, from.z);
      tr_pos = Vector3f(from.x, to.y, from.z);
    } break;
    case BlockFace::South: {
      bl_pos = Vector3f(from.x, from.y, to.z);
      br_pos = Vector3f(to.x, from.y, to.z);
      tl_pos = Vector3f(from.x, to.y, to.z);
      tr_pos = Vector3f(to.x, to.y, to.z);
    } break;
    case BlockFace::West: {
      bl_pos = Vector3f(from.x, from.y, from.z);
      br_pos = Vector3f(from.x, from.y, to.z);
      tl_pos = Vector3f(from.x, to.y, from.z);
      tr_pos = Vector3f(from.x, to.y, to.z);
    } break;
    case BlockFace::East: {
      bl_pos = Vector3f(to.x, from.y, to.z);
      br_pos = Vector3f(to.x, from.y, from.z);
      tl_pos = Vector3f(to.x, to.y, to.z);
      tr_pos = Vector3f(to.x, to.y, from.z);
    } break;
    }
  }

  inline static void SetUVs(const Vector2f& uv_from, const Vector2f& uv_to, BlockFace direction, Vector2f& bl_uv,
                            Vector2f& br_uv, Vector2f& tl_uv, Vector2f& tr_uv) {
    switch (direction) {
    case BlockFace::Down: {
      bl_uv = Vector2f(uv_to.x, uv_to.y);
      br_uv = Vector2f(uv_to.x, uv_from.y);
      tr_uv = Vector2f(uv_from.x, uv_from.y);
      tl_uv = Vector2f(uv_from.x, uv_to.y);
    } break;
    case BlockFace::Up: {
      bl_uv = Vector2f(uv_from.x, uv_from.y);
      br_uv = Vector2f(uv_from.x, uv_to.y);
      tr_uv = Vector2f(uv_to.x, uv_to.y);
      tl_uv = Vector2f(uv_to.x, uv_from.y);
    } break;
    case BlockFace::North: {
      bl_uv = Vector2f(uv_from.x, uv_to.y);
      br_uv = Vector2f(uv_to.x, uv_to.y);
      tr_uv = Vector2f(uv_to.x, uv_from.y);
      tl_uv = Vector2f(uv_from.x, uv_from.y);
    } break;
    case BlockFace::South: {
      bl_uv = Vector2f(uv_from.x, uv_to.y);
      br_uv = Vector2f(uv_to.x, uv_to.y);
      tr_uv = Vector2f(uv_to.x, uv_from.y);
      tl_uv = Vector2f(uv_from.x, uv_from.y);
    } break;
    case BlockFace::West: {
      bl_uv = Vector2f(uv_from.x, uv_to.y);
      br_uv = Vector2f(uv_to.x, uv_to.y);
      tr_uv = Vector2f(uv_to.x, uv_from.y);
      tl_uv = Vector2f(uv_from.x, uv_from.y);
    } break;
    case BlockFace::East: {
      bl_uv = Vector2f(uv_from.x, uv_to.y);
      br_uv = Vector2f(uv_to.x, uv_to.y);
      tr_uv = Vector2f(uv_to.x, uv_from.y);
      tl_uv = Vector2f(uv_from.x, uv_from.y);
    } break;
    }
  }

  inline static void Rotate0(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br,
                             Vector2f& tl, Vector2f& tr, bool set_uvs) {
    if (set_uvs) {
      SetUVs(from, to, direction, bl, br, tl, tr);
    }
  }

  inline static void Rotate90(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br,
                              Vector2f& tl, Vector2f& tr, bool set_uvs) {
    if (set_uvs) {
      float temp = from.x;

      from.x = 1.0f - from.y;
      from.y = to.x;
      to.x = 1.0f - to.y;
      to.y = temp;

      Vector2f t = to;
      to = from;
      from = t;

      SetUVs(from, to, direction, bl, br, tl, tr);
    }

    Vector2f obl = bl;

    bl = tl;
    tl = tr;
    tr = br;
    br = obl;
  }

  inline static void Rotate180(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br,
                               Vector2f& tl, Vector2f& tr, bool set_uvs) {
    if (set_uvs) {
      from.x = 1.0f - from.x;
      from.y = 1.0f - from.y;
      to.x = 1.0f - to.x;
      to.y = 1.0f - to.y;

      SetUVs(from, to, direction, bl, br, tl, tr);
    }
  }

  inline static void Rotate270(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br,
                               Vector2f& tl, Vector2f& tr, bool set_uvs) {
    if (set_uvs) {
      from.x = 1.0f - from.x;
      from.y = 1.0f - from.y;
      to.x = 1.0f - to.x;
      to.y = 1.0f - to.y;
    }

    Rotate90(direction, from, to, bl, br, tl, tr, set_uvs);
  }

  inline static void Rotate270_f(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br,
                                 Vector2f& tl, Vector2f& tr, bool set_uvs) {
    if (set_uvs) {
      float temp = from.x;

      from.x = 1.0f - from.y;
      from.y = to.x;
      to.x = 1.0f - to.y;
      to.y = temp;

      Vector2f t = to;
      to = from;
      from = t;

      SetUVs(from, to, direction, bl, br, tl, tr);
    }

    Vector2f obl = bl;

    bl = br;
    br = tr;
    tr = tl;
    tl = obl;
  }

  void CalculateUVs(BlockElement& element, RenderableFace& face, BlockFace direction) {
    int angle_x = element.variant_rotation.x;
    int angle_y = element.variant_rotation.y;

    if (element.rotation.axis.x > 0) {
      angle_x += element.rotation.angle;
    } else if (element.rotation.axis.y > 0) {
      angle_y += element.rotation.angle;
    }

    if (angle_x < 0) angle_x += 360;
    if (angle_y < 0) angle_y += 360;
    if (angle_x >= 360) angle_x -= 360;
    if (angle_y >= 360) angle_y -= 360;

    int x_index = angle_x / 90;
    int y_index = angle_y / 90;

    size_t index = x_index * 6 * 4 + y_index * 6 + (size_t)direction;

    typedef void (*RotateFunc)(BlockFace, Vector2f&, Vector2f&, Vector2f&, Vector2f&, Vector2f&, Vector2f&, bool);

    // Lookup table sorted by x, y, face for calculating locked uvs.
    // TODO: Most of these need to be verified
    static const RotateFunc kLockedRotators[] = {
        // Down    Up         North      South      West       East
        Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y0
        Rotate270, Rotate90,  Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y90
        Rotate180, Rotate180, Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y180
        Rotate90,  Rotate270, Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y270

        Rotate90,  Rotate180, Rotate90,  Rotate270, Rotate180, Rotate180, // X90   Y0
        Rotate90,  Rotate180, Rotate90,  Rotate270, Rotate180, Rotate180, // X90   Y90
        Rotate0,   Rotate90,  Rotate0,   Rotate180, Rotate270, Rotate90,  // X90   Y180
        Rotate0,   Rotate90,  Rotate270, Rotate270, Rotate270, Rotate90,  // X90   Y270

        Rotate0,   Rotate0,   Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y0
        Rotate90,  Rotate270, Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y90
        Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y180
        Rotate270, Rotate90,  Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y270

        Rotate180, Rotate0,   Rotate180, Rotate0,   Rotate90,  Rotate270, // X270 Y0
        Rotate180, Rotate0,   Rotate270, Rotate270, Rotate90,  Rotate270, // X270 Y90
        Rotate180, Rotate0,   Rotate0,   Rotate180, Rotate90,  Rotate270, // X270 Y180
        Rotate180, Rotate0,   Rotate90,  Rotate90,  Rotate90,  Rotate270, // X270 Y270
    };

    static const RotateFunc kRotators[] = {
        // Down    Up         North      South      West       East
        Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y0
        Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y90
        Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y180
        Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0     Y270

        Rotate90,  Rotate90, Rotate90,  Rotate270, Rotate180, Rotate180, // X90   Y0
        Rotate90,  Rotate90, Rotate90,  Rotate270, Rotate180, Rotate180, // X90   Y90
        Rotate0,   Rotate90,  Rotate0,   Rotate180, Rotate270, Rotate90,  // X90   Y180
        Rotate0,   Rotate90,  Rotate270, Rotate270, Rotate270, Rotate90,  // X90   Y270

        Rotate0,   Rotate0,   Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y0
        Rotate90,  Rotate270, Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y90
        Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y180
        Rotate270, Rotate90,  Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y270

        Rotate180, Rotate0,   Rotate180, Rotate0,   Rotate90,  Rotate270, // X270 Y0
        Rotate180, Rotate0,   Rotate270, Rotate270, Rotate90,  Rotate270, // X270 Y90
        Rotate180, Rotate0,   Rotate0,   Rotate180, Rotate90,  Rotate270, // X270 Y180
        Rotate180, Rotate0,   Rotate90,  Rotate90,  Rotate90,  Rotate270, // X270 Y270
    };

    Vector2f from = face.uv_from;
    Vector2f to = face.uv_to;

    BlockFace rotated = direction;

    if (uvlock) {
      kLockedRotators[index](direction, from, to, bl_uv, br_uv, tl_uv, tr_uv, true);
    } else {
      rotated = GetDirectionFace(this->direction);

      kRotators[index](rotated, from, to, bl_uv, br_uv, tl_uv, tr_uv, true);
    }

    if (face.rotation > 0) {
      static const RotateFunc kFaceRotators[] = {
          // Down    Up         North      South      West       East
          Rotate0,   Rotate0,   Rotate0,     Rotate0,     Rotate0,     Rotate0,     // Rot0
          Rotate90,  Rotate90,  Rotate270_f, Rotate270_f, Rotate270_f, Rotate270_f, // Rot90
          Rotate180, Rotate180, Rotate180,   Rotate180,   Rotate180,   Rotate180,   // Rot180
          Rotate270, Rotate270, Rotate90,    Rotate90,    Rotate90,    Rotate90,    // Rot270
      };

      size_t rot_index = (face.rotation / 90) * 6 + (size_t)rotated;
      assert(rot_index < polymer_array_count(kFaceRotators));

      kFaceRotators[rot_index](rotated, from, to, bl_uv, br_uv, tl_uv, tr_uv, false);
    }
  }

  void Mesh(BlockRegistry& registry, BorderedChunk* bordered_chunk, PushContext& context, BlockModel* model,
            BlockElement* element, const Vector3f& chunk_base, const Vector3f& relative_base, BlockFace direction) {
    RenderableFace* face = element->faces + (size_t)direction;

    if (!face->render) return;

    this->uvlock = element->rotation.uvlock;

    SetPositions(element->from, element->to, direction);

    RotateFace(*model, *element, chunk_base + relative_base);

    int ele_ao_bl = 3;
    int ele_ao_br = 3;
    int ele_ao_tl = 3;
    int ele_ao_tr = 3;

    bool shaded_axis =
        this->direction.y < -0.5f || (fabsf(this->direction.x) > 0.5f && fabsf(this->direction.z) < 0.5f);

    if (model->ambient_occlusion) {
      ele_ao_bl = GetAmbientOcclusion(registry, *bordered_chunk, relative_base, bl_lookups);
      ele_ao_br = GetAmbientOcclusion(registry, *bordered_chunk, relative_base, br_lookups);
      ele_ao_tl = GetAmbientOcclusion(registry, *bordered_chunk, relative_base, tl_lookups);
      ele_ao_tr = GetAmbientOcclusion(registry, *bordered_chunk, relative_base, tr_lookups);
    }

    if (element->shade) {
      u32 l_bl = CalculateVertexLight(bordered_chunk, relative_base, bl_lookups);
      u32 l_br = CalculateVertexLight(bordered_chunk, relative_base, br_lookups);
      u32 l_tl = CalculateVertexLight(bordered_chunk, relative_base, tl_lookups);
      u32 l_tr = CalculateVertexLight(bordered_chunk, relative_base, tr_lookups);

      ele_ao_bl |= (l_bl << 2);
      ele_ao_br |= (l_br << 2);
      ele_ao_tl |= (l_tl << 2);
      ele_ao_tr |= (l_tr << 2);
    } else {
      u32 shared_light = CalculateSharedLight(bordered_chunk, relative_base);

      ele_ao_bl |= (shared_light << 2);
      ele_ao_br |= (shared_light << 2);
      ele_ao_tl |= (shared_light << 2);
      ele_ao_tr |= (shared_light << 2);
      shaded_axis = false;
    }

    CalculateUVs(*element, *face, direction);

    if (face->random_flip) {
      u32 world_x = (u32)(chunk_base.x + relative_base.x);
      u32 world_y = (u32)(chunk_base.y + relative_base.y);
      u32 world_z = (u32)(chunk_base.z + relative_base.z);

      RandomizeFaceTexture(world_x, world_y, world_z, bl_uv, br_uv, tr_uv, tl_uv);
    }

    u16 bli = PushVertex(context, bl_pos, bl_uv, face, ele_ao_bl, shaded_axis);
    u16 bri = PushVertex(context, br_pos, br_uv, face, ele_ao_br, shaded_axis);
    u16 tri = PushVertex(context, tr_pos, tr_uv, face, ele_ao_tr, shaded_axis);
    u16 tli = PushVertex(context, tl_pos, tl_uv, face, ele_ao_tl, shaded_axis);

    PushIndex(context, face->render_layer, bli);
    PushIndex(context, face->render_layer, bri);
    PushIndex(context, face->render_layer, tri);

    PushIndex(context, face->render_layer, tri);
    PushIndex(context, face->render_layer, tli);
    PushIndex(context, face->render_layer, bli);
  }

  inline static BlockFace GetDirectionFace(const Vector3f& direction) {
    if (direction.y < -0.5f) {
      return BlockFace::Down;
    } else if (direction.y >= 0.5f) {
      return BlockFace::Up;
    } else if (direction.x < -0.5f) {
      return BlockFace::West;
    } else if (direction.x >= 0.5f) {
      return BlockFace::East;
    } else if (direction.z < -0.5f) {
      return BlockFace::North;
    } else if (direction.z >= 0.5f) {
      return BlockFace::South;
    }
    return BlockFace::Down;
  }
};

static void MeshBlock(BlockMesher& mesher, PushContext& context, BlockRegistry& block_registry,
                      BorderedChunk* bordered_chunk, u32 bid, size_t relative_x, size_t relative_y, size_t relative_z,
                      const Vector3f& chunk_base) {
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

  u32 above_id = bordered_chunk->blocks[above_index];
  u32 below_id = bordered_chunk->blocks[below_index];
  u32 north_id = bordered_chunk->blocks[north_index];
  u32 south_id = bordered_chunk->blocks[south_index];
  u32 east_id = bordered_chunk->blocks[east_index];
  u32 west_id = bordered_chunk->blocks[west_index];

  BlockModel* above_model = &block_registry.states[above_id].model;
  BlockModel* below_model = &block_registry.states[below_id].model;

  BlockModel* north_model = &block_registry.states[north_id].model;
  BlockModel* south_model = &block_registry.states[south_id].model;
  BlockModel* east_model = &block_registry.states[east_id].model;
  BlockModel* west_model = &block_registry.states[west_id].model;

  Vector3f relative_pos((float)relative_x, (float)relative_y, (float)relative_z);
  Vector3f world_pos = chunk_base + relative_pos;

  if (!IsOccluding(model, above_model, BlockFace::Up)) {
    for (size_t i = 0; i < model->element_count; ++i) {
      BlockElement* element = model->elements + i;

      FaceMesh face_mesh = {
          {Vector3f(-1, 1, 0), Vector3f(0, 1, -1), Vector3f(-1, 1, -1)},
          {Vector3f(-1, 1, 0), Vector3f(0, 1, 1), Vector3f(-1, 1, 1)},
          {Vector3f(1, 1, 0), Vector3f(0, 1, -1), Vector3f(1, 1, -1)},
          {Vector3f(1, 1, 0), Vector3f(0, 1, 1), Vector3f(1, 1, 1)},
          Vector3f(0, 1, 0),
      };

      face_mesh.Mesh(block_registry, bordered_chunk, context, model, element, chunk_base, relative_pos, BlockFace::Up);
    }
  }

  if (!IsOccluding(model, below_model, BlockFace::Down)) {
    for (size_t i = 0; i < model->element_count; ++i) {
      BlockElement* element = model->elements + i;
      FaceMesh face_mesh = {
          {Vector3f(0, -1, -1), Vector3f(1, -1, 0), Vector3f(1, -1, -1)},
          {Vector3f(0, -1, 1), Vector3f(1, -1, 0), Vector3f(1, -1, 1)},
          {Vector3f(0, -1, -1), Vector3f(-1, -1, 0), Vector3f(-1, -1, -1)},
          {Vector3f(0, -1, 1), Vector3f(-1, -1, 0), Vector3f(-1, -1, 1)},
          Vector3f(0, -1, 0),
      };

      face_mesh.Mesh(block_registry, bordered_chunk, context, model, element, chunk_base, relative_pos,
                     BlockFace::Down);
    }
  }

  if (!IsOccluding(model, north_model, BlockFace::North)) {
    for (size_t i = 0; i < model->element_count; ++i) {
      BlockElement* element = model->elements + i;
      FaceMesh face_mesh = {
          {Vector3f(1, 0, -1), Vector3f(0, -1, -1), Vector3f(1, -1, -1)},
          {Vector3f(-1, 0, -1), Vector3f(0, -1, -1), Vector3f(-1, -1, -1)},
          {Vector3f(1, 0, -1), Vector3f(0, 1, -1), Vector3f(1, 1, -1)},
          {Vector3f(-1, 0, -1), Vector3f(0, 1, -1), Vector3f(-1, 1, -1)},
          Vector3f(0, 0, -1),
      };

      face_mesh.Mesh(block_registry, bordered_chunk, context, model, element, chunk_base, relative_pos,
                     BlockFace::North);
    }
  }

  if (!IsOccluding(model, south_model, BlockFace::South)) {
    for (size_t i = 0; i < model->element_count; ++i) {
      BlockElement* element = model->elements + i;
      FaceMesh face_mesh = {
          {Vector3f(-1, 0, 1), Vector3f(0, -1, 1), Vector3f(-1, -1, 1)},
          {Vector3f(1, 0, 1), Vector3f(0, -1, 1), Vector3f(1, -1, 1)},

          {Vector3f(-1, 0, 1), Vector3f(0, 1, 1), Vector3f(-1, 1, 1)},
          {Vector3f(1, 0, 1), Vector3f(0, 1, 1), Vector3f(1, 1, 1)},

          Vector3f(0, 0, 1),
      };

      face_mesh.Mesh(block_registry, bordered_chunk, context, model, element, chunk_base, relative_pos,
                     BlockFace::South);
    }
  }

  if (!IsOccluding(model, west_model, BlockFace::West)) {
    for (size_t i = 0; i < model->element_count; ++i) {
      BlockElement* element = model->elements + i;
      FaceMesh face_mesh = {
          {Vector3f(-1, -1, 0), Vector3f(-1, 0, -1), Vector3f(-1, -1, -1)},
          {Vector3f(-1, -1, 0), Vector3f(-1, 0, 1), Vector3f(-1, -1, 1)},

          {Vector3f(-1, 1, 0), Vector3f(-1, 0, -1), Vector3f(-1, 1, -1)},
          {Vector3f(-1, 1, 0), Vector3f(-1, 0, 1), Vector3f(-1, 1, 1)},

          Vector3f(-1, 0, 0),
      };

      face_mesh.Mesh(block_registry, bordered_chunk, context, model, element, chunk_base, relative_pos,
                     BlockFace::West);
    }
  }

  if (!IsOccluding(model, east_model, BlockFace::East)) {
    for (size_t i = 0; i < model->element_count; ++i) {
      BlockElement* element = model->elements + i;
      FaceMesh face_mesh = {
          {Vector3f(1, 0, 1), Vector3f(1, -1, 0), Vector3f(1, -1, 1)},
          {Vector3f(1, -1, 0), Vector3f(1, 0, -1), Vector3f(1, -1, -1)},

          {Vector3f(1, 1, 0), Vector3f(1, 0, 1), Vector3f(1, 1, 1)},
          {Vector3f(1, 1, 0), Vector3f(1, 0, -1), Vector3f(1, 1, -1)},

          Vector3f(1, 0, 0),
      };

      face_mesh.Mesh(block_registry, bordered_chunk, context, model, element, chunk_base, relative_pos,
                     BlockFace::East);
    }
  }
}

// TODO: Real implementation.
static void MeshFluid(BlockMesher& mesher, PushContext& context, BlockRegistry& block_registry,
                      BorderedChunk* bordered_chunk, u32 bid, size_t relative_x, size_t relative_y, size_t relative_z,
                      const Vector3f& chunk_base, asset::TextureIdRange texture_range, u32 tintindex,
                      RenderLayer layer) {
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

  u32 above_id = bordered_chunk->blocks[above_index];
  u32 below_id = bordered_chunk->blocks[below_index];
  u32 north_id = bordered_chunk->blocks[north_index];
  u32 south_id = bordered_chunk->blocks[south_index];
  u32 east_id = bordered_chunk->blocks[east_index];
  u32 west_id = bordered_chunk->blocks[west_index];

  BlockModel* above_model = &block_registry.states[above_id].model;
  BlockModel* below_model = &block_registry.states[below_id].model;

  BlockModel* north_model = &block_registry.states[north_id].model;
  BlockModel* south_model = &block_registry.states[south_id].model;
  BlockModel* east_model = &block_registry.states[east_id].model;
  BlockModel* west_model = &block_registry.states[west_id].model;

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

  size_t current_index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);

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

    size_t bl_indices[] = {above_index, above_north_index, above_west_index, above_north_west_index};
    size_t br_indices[] = {above_index, above_south_index, above_west_index, above_south_west_index};
    size_t tl_indices[] = {above_index, above_north_index, above_east_index, above_north_east_index};
    size_t tr_indices[] = {above_index, above_south_index, above_east_index, above_south_east_index};

    u32 l_bl = CalculateVertexLight(bordered_chunk, bl_indices, current_index);
    u32 l_br = CalculateVertexLight(bordered_chunk, br_indices, current_index);
    u32 l_tl = CalculateVertexLight(bordered_chunk, tl_indices, current_index);
    u32 l_tr = CalculateVertexLight(bordered_chunk, tr_indices, current_index);

    u32 ele_ao_bl = (l_bl << 2) | 3;
    u32 ele_ao_br = (l_br << 2) | 3;
    u32 ele_ao_tl = (l_tl << 2) | 3;
    u32 ele_ao_tr = (l_tr << 2) | 3;

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

    size_t bl_indices[] = {below_index, below_north_index, below_east_index, below_north_east_index};
    size_t br_indices[] = {below_index, below_south_index, below_east_index, below_south_east_index};
    size_t tl_indices[] = {below_index, below_north_index, below_west_index, below_north_west_index};
    size_t tr_indices[] = {below_index, below_south_index, below_west_index, below_south_west_index};

    u32 l_bl = CalculateVertexLight(bordered_chunk, bl_indices, current_index);
    u32 l_br = CalculateVertexLight(bordered_chunk, br_indices, current_index);
    u32 l_tl = CalculateVertexLight(bordered_chunk, tl_indices, current_index);
    u32 l_tr = CalculateVertexLight(bordered_chunk, tr_indices, current_index);

    u32 ele_ao_bl = (l_bl << 2) | 3;
    u32 ele_ao_br = (l_br << 2) | 3;
    u32 ele_ao_tl = (l_tl << 2) | 3;
    u32 ele_ao_tr = (l_tr << 2) | 3;

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

    size_t bl_indices[] = {north_index, north_east_index, below_north_east_index, below_north_index};
    size_t br_indices[] = {north_index, north_west_index, below_north_west_index, below_north_index};
    size_t tl_indices[] = {north_index, north_east_index, above_north_east_index, above_north_index};
    size_t tr_indices[] = {north_index, north_west_index, above_north_west_index, above_north_index};

    u32 l_bl = CalculateVertexLight(bordered_chunk, bl_indices, current_index);
    u32 l_br = CalculateVertexLight(bordered_chunk, br_indices, current_index);
    u32 l_tl = CalculateVertexLight(bordered_chunk, tl_indices, current_index);
    u32 l_tr = CalculateVertexLight(bordered_chunk, tr_indices, current_index);

    u32 ele_ao_bl = (l_bl << 2) | 3;
    u32 ele_ao_br = (l_br << 2) | 3;
    u32 ele_ao_tl = (l_tl << 2) | 3;
    u32 ele_ao_tr = (l_tr << 2) | 3;

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

    size_t bl_indices[] = {south_index, south_west_index, below_south_west_index, below_south_index};
    size_t br_indices[] = {south_index, south_east_index, below_south_east_index, below_south_index};
    size_t tl_indices[] = {south_index, south_west_index, above_south_west_index, above_south_index};
    size_t tr_indices[] = {south_index, south_east_index, above_south_east_index, above_south_index};

    u32 l_bl = CalculateVertexLight(bordered_chunk, bl_indices, current_index);
    u32 l_br = CalculateVertexLight(bordered_chunk, br_indices, current_index);
    u32 l_tl = CalculateVertexLight(bordered_chunk, tl_indices, current_index);
    u32 l_tr = CalculateVertexLight(bordered_chunk, tr_indices, current_index);

    u32 ele_ao_bl = (l_bl << 2) | 3;
    u32 ele_ao_br = (l_br << 2) | 3;
    u32 ele_ao_tl = (l_tl << 2) | 3;
    u32 ele_ao_tr = (l_tr << 2) | 3;

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

    size_t bl_indices[] = {east_index, below_east_index, below_south_east_index, south_east_index};
    size_t br_indices[] = {east_index, below_east_index, below_north_east_index, north_east_index};
    size_t tl_indices[] = {east_index, above_east_index, above_south_east_index, south_east_index};
    size_t tr_indices[] = {east_index, above_east_index, above_north_east_index, north_east_index};

    u32 l_bl = CalculateVertexLight(bordered_chunk, bl_indices, current_index);
    u32 l_br = CalculateVertexLight(bordered_chunk, br_indices, current_index);
    u32 l_tl = CalculateVertexLight(bordered_chunk, tl_indices, current_index);
    u32 l_tr = CalculateVertexLight(bordered_chunk, tr_indices, current_index);

    u32 ele_ao_bl = (l_bl << 2) | 3;
    u32 ele_ao_br = (l_br << 2) | 3;
    u32 ele_ao_tl = (l_tl << 2) | 3;
    u32 ele_ao_tr = (l_tr << 2) | 3;

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

    size_t bl_indices[] = {west_index, below_west_index, below_north_west_index, north_west_index};
    size_t br_indices[] = {west_index, below_west_index, below_south_west_index, south_west_index};
    size_t tl_indices[] = {west_index, above_west_index, above_north_west_index, north_west_index};
    size_t tr_indices[] = {west_index, above_west_index, above_south_west_index, south_west_index};

    u32 l_bl = CalculateVertexLight(bordered_chunk, bl_indices, current_index);
    u32 l_br = CalculateVertexLight(bordered_chunk, br_indices, current_index);
    u32 l_tl = CalculateVertexLight(bordered_chunk, tl_indices, current_index);
    u32 l_tr = CalculateVertexLight(bordered_chunk, tr_indices, current_index);

    u32 ele_ao_bl = (l_bl << 2) | 3;
    u32 ele_ao_br = (l_br << 2) | 3;
    u32 ele_ao_tl = (l_tl << 2) | 3;
    u32 ele_ao_tr = (l_tr << 2) | 3;

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

  BorderedChunk* bordered_chunk = CreateBorderedChunk(trans_arena, ctx, chunk_y);
  if (!bordered_chunk) return vertex_data;

  water_texture = assets.GetTextureRange(POLY_STR("assets/minecraft/textures/block/water_still.png"));
  asset::TextureIdRange lava_texture =
      assets.GetTextureRange(POLY_STR("assets/minecraft/textures/block/lava_still.png"));

  Vector3f chunk_base(chunk_x * 16.0f, chunk_y * 16.0f - 64.0f, chunk_z * 16.0f);

  PushContext context(false);

  for (size_t i = 0; i < kRenderLayerCount; ++i) {
    RenderLayer layer = (RenderLayer)i;
    context.SetLayerData(layer, &vertex_arenas[i], &index_arenas[i]);
  }

  for (size_t relative_y = 0; relative_y < 16; ++relative_y) {
    for (size_t relative_z = 0; relative_z < 16; ++relative_z) {
      for (size_t relative_x = 0; relative_x < 16; ++relative_x) {
        size_t index = (relative_y + 1) * 18 * 18 + (relative_z + 1) * 18 + (relative_x + 1);

        u32 bid = bordered_chunk->blocks[index];

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

BorderedChunk* CreateBorderedChunk(MemoryArena& arena, ChunkBuildContext* ctx, s32 chunk_y) {
  BorderedChunk* bordered_chunk = memory_arena_push_type(&arena, BorderedChunk);

  memset(bordered_chunk, 0, sizeof(BorderedChunk));

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

        bordered_chunk->blocks[index] = section->chunks[chunk_y].blocks[y][z][x];
        bordered_chunk->lightmap[index] = section->chunks[chunk_y].lightmap[y][z][x];
      }
    }
  }

  // Load west blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + 0);

      bordered_chunk->blocks[index] = west_section->chunks[chunk_y].blocks[y][z][15];
      bordered_chunk->lightmap[index] = west_section->chunks[chunk_y].lightmap[y][z][15];
    }
  }

  // Load east blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)((y + 1) * 18 * 18 + (z + 1) * 18 + 17);

      bordered_chunk->blocks[index] = east_section->chunks[chunk_y].blocks[y][z][0];
      bordered_chunk->lightmap[index] = east_section->chunks[chunk_y].lightmap[y][z][0];
    }
  }

  // Load north blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)((y + 1) * 18 * 18 + (x + 1));

      bordered_chunk->blocks[index] = north_section->chunks[chunk_y].blocks[y][15][x];
      bordered_chunk->lightmap[index] = north_section->chunks[chunk_y].lightmap[y][15][x];
    }
  }

  // Load south blocks
  for (s64 y = 0; y < 16; ++y) {
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + (x + 1));

      bordered_chunk->blocks[index] = south_section->chunks[chunk_y].blocks[y][0][x];
      bordered_chunk->lightmap[index] = south_section->chunks[chunk_y].lightmap[y][0][x];
    }
  }

  // South-east corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + 17);

    bordered_chunk->blocks[index] = south_east_section->chunks[chunk_y].blocks[y][0][0];
    bordered_chunk->lightmap[index] = south_east_section->chunks[chunk_y].lightmap[y][0][0];
  }

  // North-east corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 0 * 18 + 17);

    bordered_chunk->blocks[index] = north_east_section->chunks[chunk_y].blocks[y][15][0];
    bordered_chunk->lightmap[index] = north_east_section->chunks[chunk_y].lightmap[y][15][0];
  }

  // South-west corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 17 * 18 + 0);

    bordered_chunk->blocks[index] = south_west_section->chunks[chunk_y].blocks[y][0][15];
    bordered_chunk->lightmap[index] = south_west_section->chunks[chunk_y].lightmap[y][0][15];
  }

  // North-west corner
  for (size_t y = 0; y < 16; ++y) {
    size_t index = (size_t)((y + 1) * 18 * 18 + 0 * 18 + 0);

    bordered_chunk->blocks[index] = north_west_section->chunks[chunk_y].blocks[y][15][15];
    bordered_chunk->lightmap[index] = north_west_section->chunks[chunk_y].lightmap[y][15][15];
  }

  if (chunk_y < kChunkColumnCount) {
    // Load above blocks
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + (x + 1));

        bordered_chunk->blocks[index] = section->chunks[chunk_y + 1].blocks[0][z][x];
        bordered_chunk->lightmap[index] = section->chunks[chunk_y + 1].lightmap[0][z][x];
      }
    }

    // Load above-south
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)(17 * 18 * 18 + 17 * 18 + (x + 1));

      bordered_chunk->blocks[index] = south_section->chunks[chunk_y + 1].blocks[0][0][x];
      bordered_chunk->lightmap[index] = south_section->chunks[chunk_y + 1].lightmap[0][0][x];
    }

    // Load above-north
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)(17 * 18 * 18 + 0 * 18 + (x + 1));

      bordered_chunk->blocks[index] = north_section->chunks[chunk_y + 1].blocks[0][15][x];
      bordered_chunk->lightmap[index] = north_section->chunks[chunk_y + 1].lightmap[0][15][x];
    }

    // Load above-east
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + 17);

      bordered_chunk->blocks[index] = east_section->chunks[chunk_y + 1].blocks[0][z][0];
      bordered_chunk->lightmap[index] = east_section->chunks[chunk_y + 1].lightmap[0][z][0];
    }

    // Load above-west
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)(17 * 18 * 18 + (z + 1) * 18 + 0);

      bordered_chunk->blocks[index] = west_section->chunks[chunk_y + 1].blocks[0][z][15];
      bordered_chunk->lightmap[index] = west_section->chunks[chunk_y + 1].lightmap[0][z][15];
    }

    {
      // Load above-south-east
      size_t index = (size_t)(17 * 18 * 18 + 17 * 18 + 17);

      bordered_chunk->blocks[index] = south_east_section->chunks[chunk_y + 1].blocks[0][0][0];
      bordered_chunk->lightmap[index] = south_east_section->chunks[chunk_y + 1].lightmap[0][0][0];
    }

    {
      size_t index = (size_t)(17 * 18 * 18 + 17 * 18 + 0);

      // Load above-south-west
      bordered_chunk->blocks[index] = south_west_section->chunks[chunk_y + 1].blocks[0][0][15];
      bordered_chunk->lightmap[index] = south_west_section->chunks[chunk_y + 1].lightmap[0][0][15];
    }

    {
      size_t index = (size_t)(17 * 18 * 18 + 0 * 18 + 17);

      // Load above-north-east
      bordered_chunk->blocks[index] = north_east_section->chunks[chunk_y + 1].blocks[0][15][0];
      bordered_chunk->lightmap[index] = north_east_section->chunks[chunk_y + 1].lightmap[0][15][0];
    }

    {
      size_t index = (size_t)(17 * 18 * 18 + 0 * 18 + 0);

      // Load above-north-west
      bordered_chunk->blocks[index] = north_west_section->chunks[chunk_y + 1].blocks[0][15][15];
      bordered_chunk->lightmap[index] = north_west_section->chunks[chunk_y + 1].lightmap[0][15][15];
    }
  }

  if (chunk_y > 0) {
    // Load below blocks
    for (s64 z = 0; z < 16; ++z) {
      for (s64 x = 0; x < 16; ++x) {
        size_t index = (size_t)((z + 1) * 18 + (x + 1));

        bordered_chunk->blocks[index] = section->chunks[chunk_y - 1].blocks[15][z][x];
        bordered_chunk->lightmap[index] = section->chunks[chunk_y - 1].lightmap[15][z][x];
      }
    }

    // Load below-south
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)(0 * 18 * 18 + 17 * 18 + (x + 1));

      bordered_chunk->blocks[index] = south_section->chunks[chunk_y - 1].blocks[15][0][x];
      bordered_chunk->lightmap[index] = south_section->chunks[chunk_y - 1].lightmap[15][0][x];
    }

    // Load below-north
    for (s64 x = 0; x < 16; ++x) {
      size_t index = (size_t)(0 * 18 * 18 + 0 * 18 + (x + 1));

      bordered_chunk->blocks[index] = north_section->chunks[chunk_y - 1].blocks[15][15][x];
      bordered_chunk->lightmap[index] = north_section->chunks[chunk_y - 1].lightmap[15][15][x];
    }

    // Load below-east
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)(0 * 18 * 18 + (z + 1) * 18 + 17);

      bordered_chunk->blocks[index] = east_section->chunks[chunk_y - 1].blocks[15][z][0];
      bordered_chunk->lightmap[index] = east_section->chunks[chunk_y - 1].lightmap[15][z][0];
    }

    // Load below-west
    for (s64 z = 0; z < 16; ++z) {
      size_t index = (size_t)(0 * 18 * 18 + (z + 1) * 18 + 0);

      bordered_chunk->blocks[index] = west_section->chunks[chunk_y - 1].blocks[15][z][15];
      bordered_chunk->lightmap[index] = west_section->chunks[chunk_y - 1].lightmap[15][z][15];
    }

    {
      size_t index = (size_t)(0 * 18 * 18 + 17 * 18 + 17);

      // Load below-south-east
      bordered_chunk->blocks[index] = south_east_section->chunks[chunk_y - 1].blocks[15][0][0];
      bordered_chunk->lightmap[index] = south_east_section->chunks[chunk_y - 1].lightmap[15][0][0];
    }

    {
      size_t index = (size_t)(0 * 18 * 18 + 17 * 18 + 0);

      // Load below-south-west
      bordered_chunk->blocks[index] = south_west_section->chunks[chunk_y - 1].blocks[15][0][15];
      bordered_chunk->lightmap[index] = south_west_section->chunks[chunk_y - 1].lightmap[15][0][15];
    }

    {
      size_t index = (size_t)(0 * 18 * 18 + 0 * 18 + 17);

      // Load below-north-east
      bordered_chunk->blocks[index] = north_east_section->chunks[chunk_y - 1].blocks[15][15][0];
      bordered_chunk->lightmap[index] = north_east_section->chunks[chunk_y - 1].lightmap[15][15][0];
    }

    {
      size_t index = (size_t)(0 * 18 * 18 + 0 * 18 + 0);

      // Load below-north-west
      bordered_chunk->blocks[index] = north_west_section->chunks[chunk_y - 1].blocks[15][15][15];
      bordered_chunk->lightmap[index] = north_west_section->chunks[chunk_y - 1].lightmap[15][15][15];
    }
  }

  return bordered_chunk;
}

} // namespace render
} // namespace polymer
