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

  struct {
    u32 render : 1;
    u32 transparency : 1;
    u32 cullface : 3;
    u32 padding : 11;
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
    u32 padding : 30;
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
};

struct BlockState {
  u32 id;
  BlockStateInfo* info;

  BlockModel model;
  float x;
  float y;
  bool uvlock;
};

} // namespace polymer

#endif
