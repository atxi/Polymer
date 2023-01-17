#ifndef POLYMER_BLOCK_H_
#define POLYMER_BLOCK_H_

#include <polymer/hashmap.h>
#include <polymer/math.h>
#include <polymer/types.h>

namespace polymer {
namespace world {

enum class BlockFace { Down, Up, North, South, West, East };

inline BlockFace GetOppositeFace(BlockFace face) {
  switch (face) {
  case BlockFace::Down:
    return BlockFace::Up;
  case BlockFace::Up:
    return BlockFace::Down;
  case BlockFace::North:
    return BlockFace::South;
  case BlockFace::South:
    return BlockFace::North;
  case BlockFace::West:
    return BlockFace::East;
  case BlockFace::East:
    return BlockFace::West;
  }

  return BlockFace::Down;
}

inline Vector3f GetFaceDirection(BlockFace face) {
  static const Vector3f kDirections[] = {
      Vector3f(0, -1, 0), Vector3f(0, 1, 0),  Vector3f(0, 0, -1),
      Vector3f(0, 0, 1),  Vector3f(-1, 0, 0), Vector3f(1, 0, 0),
  };

  return kDirections[(size_t)face];
}

struct FaceQuad {
  Vector3f bl_pos;
  Vector3f br_pos;
  Vector3f tl_pos;
  Vector3f tr_pos;

  Vector2f bl_uv;
  Vector2f br_uv;
  Vector2f tl_uv;
  Vector2f tr_uv;
};

struct RenderableFace {
  Vector2f uv_from;
  Vector2f uv_to;
  FaceQuad* quad;

  u32 texture_id;

  struct {
    u32 render : 1;
    u32 transparency : 1;
    u32 cullface : 3;
    u32 render_layer : 3;
    u32 random_flip : 1;
    u32 frame_count : 7;
    u32 tintindex : 16;
  };
};

struct ElementRotation {
  Vector3f origin;
  Vector3f axis;
  int angle;

  u8 rescale : 1;
  u8 uvlock : 1;
  u8 padding : 6;
};

struct BlockElement {
  RenderableFace faces[6];
  Vector3f from;
  Vector3f to;

  struct {
    u32 occluding : 1;
    u32 shade : 1;
    u32 rescale : 1;
    u32 padding : 29;
  };

  inline RenderableFace& GetFace(BlockFace face) {
    return faces[(size_t)face];
  }
};

struct BlockModel {
  size_t element_count;
  BlockElement elements[48];

  u32 has_occluding : 1;
  u32 has_transparency : 1;
  u32 has_shaded : 1;
  u32 has_leaves : 1;
  u32 has_glass : 1;
  u32 has_variant_rotation : 1;
  u32 ambient_occlusion : 1;
  u32 random_horizontal_offset : 1;
  u32 random_vertical_offset : 1;
  u32 padding : 23;

  inline bool HasOccluding() const {
    return has_occluding;
  }

  inline bool HasTransparency() const {
    return has_transparency;
  }

  inline bool HasShadedElement() const {
    return has_shaded;
  }
};

struct BlockStateInfo {
  char name[48];
  size_t name_length;
};

struct BlockState {
  u32 id;
  BlockStateInfo* info;

  BlockModel model;
  float x;
  float y;

  struct {
    u32 uvlock : 1;
    // TODO: Need a better way of storing the properties. Just using the blockstate flag field for fluid levels until a
    // better way is implemented.
    u32 leveled : 1;
    u32 level : 4;
    u32 padding : 26;
  };
};

struct BlockIdRange {
  u32 base;
  u32 count;

  BlockIdRange() : base(0), count(0) {}
  BlockIdRange(u32 base, u32 count) : base(base), count(count) {}

  bool Contains(u32 bid) const {
    return bid >= base && bid < base + count;
  }

  bool operator==(const BlockIdRange& other) const {
    return base == other.base && count == other.count;
  }
};

struct BlockRegistry {
  size_t state_count = 0;
  BlockState* states;

  size_t info_count = 0;
  BlockStateInfo* infos;

  size_t property_count = 0;
  String* properties;

  // Map block name to block id
  HashMap<String, BlockIdRange, MapStringHasher> name_map;

  BlockRegistry(MemoryArena& arena) : name_map(arena) {}
};

} // namespace world
} // namespace polymer

#endif
