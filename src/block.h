#ifndef POLYMER_BLOCK_H_
#define POLYMER_BLOCK_H_

#include "math.h"
#include "types.h"

namespace polymer {

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

struct RenderableFace {
  Vector2f uv_from;
  Vector2f uv_to;

  u32 texture_id;
  u32 frame_count;

  struct {
    u32 render : 1;
    u32 transparency : 1;
    u32 cullface : 3;
    u32 render_layer : 3;
    u32 random_flip : 1;
    u32 padding : 7;
    u32 tintindex : 16;
  };
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
};

struct BlockModel {
  size_t element_count;
  BlockElement elements[20];

  bool IsOccluding() {
    for (size_t i = 0; i < element_count; ++i) {
      if (elements[i].occluding) {
        return true;
      }
    }

    return false;
  }

  bool HasShadedElement() {
    for (size_t i = 0; i < element_count; ++i) {
      if (elements[i].shade) {
        return true;
      }
    }
    return false;
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

struct BlockRegistry {
  size_t state_count;
  BlockState* states;

  size_t info_count;
  BlockStateInfo* infos;
};

} // namespace polymer

#endif
