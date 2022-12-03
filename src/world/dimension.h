#ifndef POLYMER_DIMENSION_H_
#define POLYMER_DIMENSION_H_

#include "../types.h"

namespace polymer {

struct MemoryArena;

namespace nbt {

struct TagCompound;

} // namespace nbt

namespace world {

enum DimensionFlags {
  DimensionFlag_PiglinSafe = (1 << 0),
  DimensionFlag_Natural = (1 << 1),
  DimensionFlag_RespawnAnchor = (1 << 2),
  DimensionFlag_HasSkylight = (1 << 3),
  DimensionFlag_BedWorks = (1 << 4),
  DimensionFlag_HasRaids = (1 << 5),
  DimensionFlag_Ultrawarm = (1 << 6),
  DimensionFlag_HasCeiling = (1 << 7),
};

struct DimensionType {
  String name;
  String infiniburn;
  String effects;

  s32 id;
  u32 flags;

  s32 min_y;
  s32 height;

  s32 logical_height;
  float ambient_light;

  double coordinate_scale;

  u64 fixed_time;
};

struct DimensionCodec {
  DimensionType* types;
  size_t type_count;

  void Parse(MemoryArena& arena, nbt::TagCompound& nbt);
  void ParseType(MemoryArena& arena, nbt::TagCompound& nbt, DimensionType* type);

  DimensionType* GetDimensionType(const String& identifier);
};

} // namespace world
} // namespace polymer

#endif
