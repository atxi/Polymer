#include <polymer/asset/block_model_rotate.h>

#include <polymer/memory.h>

#include <assert.h>

using namespace polymer::world;

namespace polymer {
namespace asset {

void CalculateUVs(const Vector3i& variant_rotation, const ElementRotation& element_rotation,
                  const ParsedRenderableFace& renderable_face, struct Face& face, BlockFace direction, bool uvlock);

inline Vector3f GetFaceDirection(BlockFace face) {
  static const Vector3f kDirections[] = {
      Vector3f(0, -1, 0), Vector3f(0, 1, 0),  Vector3f(0, 0, -1),
      Vector3f(0, 0, 1),  Vector3f(-1, 0, 0), Vector3f(1, 0, 0),
  };

  return kDirections[(size_t)face];
}

inline static bool GetDirectionFace(const Vector3f& direction, BlockFace* face) {
  if (direction.y < -0.9f) {
    *face = BlockFace::Down;
    return true;
  } else if (direction.y >= 0.9f) {
    *face = BlockFace::Up;
    return true;
  } else if (direction.x < -0.9f) {
    *face = BlockFace::West;
    return true;
  } else if (direction.x >= 0.9f) {
    *face = BlockFace::East;
    return true;
  } else if (direction.z < -0.9f) {
    *face = BlockFace::North;
    return true;
  } else if (direction.z >= 0.9f) {
    *face = BlockFace::South;
    return true;
  }

  return false;
}

struct Face {
  Vector3f bl_pos;
  Vector3f br_pos;
  Vector3f tl_pos;
  Vector3f tr_pos;

  Vector2f bl_uv;
  Vector2f br_uv;
  Vector2f tl_uv;
  Vector2f tr_uv;

  Vector3f direction;

  Face(BlockFace block_face, const Vector3f& from, const Vector3f& to) {
    switch (block_face) {
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

    this->direction = GetFaceDirection(block_face);
  }

  void Rotate(float angle, const Vector3f& axis, const Vector3f& origin) {
    bl_pos = ::polymer::Rotate(bl_pos - origin, angle, axis) + origin;
    br_pos = ::polymer::Rotate(br_pos - origin, angle, axis) + origin;
    tl_pos = ::polymer::Rotate(tl_pos - origin, angle, axis) + origin;
    tr_pos = ::polymer::Rotate(tr_pos - origin, angle, axis) + origin;

    direction = ::polymer::Rotate(direction, angle, axis);
  }
};

Face RotateFace(const ParsedBlockElement& element, const ParsedRenderableFace& parsed_face, Vector3f& direction,
                const Vector3i& variant_rotation, BlockFace block_face) {
  Vector3f ele_axis = element.rotation.axis;
  Vector3f ele_origin = element.rotation.origin;
  Vector3f origin(0.5f, 0.5f, 0.5f);

  Face face(block_face, element.from, element.to);

  // Rotate the elements by the variant rotation before rotating the actual elements
  if (variant_rotation.x) {
    float angle = -Radians((float)variant_rotation.x);
    Vector3f axis(1, 0, 0);

    face.Rotate(angle, axis, origin);

    ele_axis = Rotate(ele_axis, angle, axis);
    ele_origin = Rotate(ele_origin - origin, angle, axis) + origin;
  }

  if (variant_rotation.y) {
    float angle = -Radians((float)variant_rotation.y);
    Vector3f axis(0, 1, 0);

    face.Rotate(angle, axis, origin);

    ele_axis = Rotate(ele_axis, angle, axis);
    ele_origin = Rotate(ele_origin - origin, angle, axis) + origin;
  }

  if (element.rotation.angle != 0.0f) {
    float angle = Radians((float)element.rotation.angle);

    face.Rotate(angle, ele_axis, ele_origin);

    if (element.rotation.rescale) {
      float scale = (1.0f / cosf(angle));

      face.bl_pos -= origin;
      face.br_pos -= origin;
      face.tl_pos -= origin;
      face.tr_pos -= origin;

      if (fabsf(element.rotation.axis.x) >= 0.5f) {
        face.bl_pos.y *= scale;
        face.br_pos.y *= scale;
        face.tl_pos.y *= scale;
        face.tr_pos.y *= scale;

        face.bl_pos.z *= scale;
        face.br_pos.z *= scale;
        face.tl_pos.z *= scale;
        face.tr_pos.z *= scale;

      } else if (fabsf(element.rotation.axis.y) >= 0.5f) {
        face.bl_pos.x *= scale;
        face.br_pos.x *= scale;
        face.tl_pos.x *= scale;
        face.tr_pos.x *= scale;

        face.bl_pos.z *= scale;
        face.br_pos.z *= scale;
        face.tl_pos.z *= scale;
        face.tr_pos.z *= scale;
      } else if (fabsf(element.rotation.axis.z) >= 0.5f) {
        face.bl_pos.x *= scale;
        face.br_pos.x *= scale;
        face.tl_pos.x *= scale;
        face.tr_pos.x *= scale;

        face.bl_pos.y *= scale;
        face.br_pos.y *= scale;
        face.tl_pos.y *= scale;
        face.tr_pos.y *= scale;
      }

      face.bl_pos += origin;
      face.br_pos += origin;
      face.tl_pos += origin;
      face.tr_pos += origin;
    }
  }

  return face;
}

bool HasRotation(BlockModel& model, const ParsedBlockModel& parsed_model, const Vector3i& rotation,
                 size_t element_start, size_t element_count) {
  if (rotation.x != 0 || rotation.y != 0) return true;

  for (size_t i = element_start; i < model.element_count && i < element_start + element_count; ++i) {
    if (parsed_model.elements[i].rotation.angle != 0) {
      return true;
    }

    for (size_t j = 0; j < 6; ++j) {
      if (parsed_model.elements[i].faces[j].rotation != 0) {
        return true;
      }
    }
  }

  return false;
}

void RotateVariant(MemoryArena& perm_arena, world::BlockModel& model, const ParsedBlockModel& parsed_model,
                   size_t element_start, size_t element_count, const Vector3i& rotation, bool uvlock) {
  // This has no variant/element/face rotations, so skip allocating anything
  if (!HasRotation(model, parsed_model, rotation, element_start, element_count)) return;

  model.has_variant_rotation = rotation.x || rotation.y;

  for (size_t i = 0; i < parsed_model.element_count && i < element_count; ++i) {
    const ParsedBlockElement& element = parsed_model.elements[i];

    for (size_t j = 0; j < 6; ++j) {
      BlockFace block_face = (BlockFace)j;

      Vector3f direction = GetFaceDirection(block_face);
      Face rotated_face = RotateFace(element, element.faces[j], direction, rotation, block_face);

      BlockFace new_block_face = block_face;
      GetDirectionFace(rotated_face.direction, &new_block_face);
      RenderableFace& new_face = model.elements[element_start + i].faces[(size_t)new_block_face];

      new_face.uv_from = element.faces[j].uv_from;
      new_face.uv_to = element.faces[j].uv_to;

      new_face.texture_id = element.faces[j].texture_id;
      new_face.frame_count = element.faces[j].frame_count;

      new_face.render = element.faces[j].render;
      new_face.transparency = element.faces[j].transparency;
      new_face.cullface = element.faces[j].cullface;
      new_face.render_layer = element.faces[j].render_layer;
      new_face.random_flip = element.faces[j].random_flip;
      new_face.tintindex = element.faces[j].tintindex;

      if (new_face.render) {
        BlockFace uv_face = uvlock ? block_face : new_block_face;

        CalculateUVs(rotation, element.rotation, element.faces[j], rotated_face, uv_face, uvlock);

        FaceQuad* quad = memory_arena_push_type(&perm_arena, FaceQuad);

        new_face.quad = quad;
        quad->bl_pos = rotated_face.bl_pos;
        quad->br_pos = rotated_face.br_pos;
        quad->tl_pos = rotated_face.tl_pos;
        quad->tr_pos = rotated_face.tr_pos;

        quad->bl_uv = rotated_face.bl_uv;
        quad->br_uv = rotated_face.br_uv;
        quad->tl_uv = rotated_face.tl_uv;
        quad->tr_uv = rotated_face.tr_uv;
      }
    }
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

inline static void Rotate0(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br, Vector2f& tl,
                           Vector2f& tr, bool set_uvs) {
  if (set_uvs) {
    SetUVs(from, to, direction, bl, br, tl, tr);
  }
}

inline static void Rotate90(BlockFace direction, Vector2f& from, Vector2f& to, Vector2f& bl, Vector2f& br, Vector2f& tl,
                            Vector2f& tr, bool set_uvs) {
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

void CalculateUVs(const Vector3i& variant_rotation, const ElementRotation& element_rotation,
                  const ParsedRenderableFace& renderable_face, Face& face, BlockFace direction, bool uvlock) {
  int angle_x = variant_rotation.x;
  int angle_y = variant_rotation.y;

  if (element_rotation.axis.x > 0) {
    angle_x += element_rotation.angle;
  } else if (element_rotation.axis.y > 0) {
    angle_y += element_rotation.angle;
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

      Rotate180, Rotate90,  Rotate90,  Rotate270, Rotate180, Rotate180, // X90   Y0
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
      Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0 Y0
      Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0 Y90
      Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0 Y180
      Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0, // X0 Y270

      Rotate90,  Rotate270, Rotate270, Rotate270, Rotate0,   Rotate0,   // X90 Y0
      Rotate90,  Rotate270, Rotate0,   Rotate0,   Rotate270, Rotate270, // X90 Y90
      Rotate0,   Rotate90,  Rotate0,   Rotate180, Rotate270, Rotate90,  // X90 Y180
      Rotate0,   Rotate90,  Rotate270, Rotate270, Rotate270, Rotate90,  // X90 Y270

      Rotate0,   Rotate0,   Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y0
      Rotate90,  Rotate270, Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y90
      Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y180
      Rotate270, Rotate90,  Rotate180, Rotate180, Rotate180, Rotate180, // X180 Y270

      Rotate180, Rotate0,   Rotate180, Rotate0,   Rotate90,  Rotate270, // X270 Y0
      Rotate180, Rotate0,   Rotate270, Rotate270, Rotate90,  Rotate270, // X270 Y90
      Rotate180, Rotate0,   Rotate0,   Rotate180, Rotate90,  Rotate270, // X270 Y180
      Rotate180, Rotate0,   Rotate90,  Rotate90,  Rotate90,  Rotate270, // X270 Y270
  };

  Vector2f from = renderable_face.uv_from;
  Vector2f to = renderable_face.uv_to;

  if (uvlock) {
    kLockedRotators[index](direction, from, to, face.bl_uv, face.br_uv, face.tl_uv, face.tr_uv, true);
  } else {
    kRotators[index](direction, from, to, face.bl_uv, face.br_uv, face.tl_uv, face.tr_uv, true);
  }

  if (renderable_face.rotation > 0) {
    static const RotateFunc kFaceRotators[] = {
        // Down    Up         North      South      West       East
        Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   Rotate0,   // Rot0
        Rotate90,  Rotate90,  Rotate90,  Rotate90,  Rotate90,  Rotate90,  // Rot90
        Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, Rotate180, // Rot180
        Rotate270, Rotate270, Rotate90,  Rotate90,  Rotate90,  Rotate90,  // Rot270
    };

    size_t rot_index = (renderable_face.rotation / 90) * 6 + (size_t)direction;
    assert(rot_index < polymer_array_count(kFaceRotators));

    kFaceRotators[rot_index](direction, from, to, face.bl_uv, face.br_uv, face.tl_uv, face.tr_uv, false);
  }
}

} // namespace asset
} // namespace polymer
