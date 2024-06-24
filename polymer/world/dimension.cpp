#include <polymer/world/dimension.h>

#include <polymer/memory.h>
#include <polymer/nbt.h>

#include <stdio.h>

namespace polymer {
namespace world {

inline void ProcessDimensionFlag(DimensionType& type, nbt::TagCompound& element_compound, String name, u32 flag) {
  nbt::Tag* flag_tag = element_compound.GetNamedTag(name);

  if (flag_tag && flag_tag->type == nbt::TagType::Byte) {
    nbt::TagByte* data_tag = (nbt::TagByte*)flag_tag->tag;

    if (data_tag->data) {
      type.flags |= flag;
    }
  }
}

template <typename DataTag, typename T>
inline void ProcessDimensionData(DimensionType& type, nbt::TagCompound& element_compound, String name,
                                 nbt::TagType tag_type, T* out) {
  nbt::Tag* named_tag = element_compound.GetNamedTag(name);

  if (named_tag && named_tag->type == tag_type) {
    DataTag* data_tag = (DataTag*)named_tag->tag;

    *out = data_tag->data;
  }
}

inline void ProcessDimensionInt(DimensionType& type, nbt::TagCompound& element_compound, String name, int* out) {
  ProcessDimensionData<nbt::TagInt, int>(type, element_compound, name, nbt::TagType::Int, out);
}

inline void ProcessDimensionFloat(DimensionType& type, nbt::TagCompound& element_compound, String name, float* out) {
  ProcessDimensionData<nbt::TagFloat, float>(type, element_compound, name, nbt::TagType::Float, out);
}

inline void ProcessDimensionDouble(DimensionType& type, nbt::TagCompound& element_compound, String name, double* out) {
  ProcessDimensionData<nbt::TagDouble, double>(type, element_compound, name, nbt::TagType::Double, out);
}

inline void ProcessDimensionLong(DimensionType& type, nbt::TagCompound& element_compound, String name, u64* out) {
  ProcessDimensionData<nbt::TagLong, u64>(type, element_compound, name, nbt::TagType::Long, out);
}

void DimensionCodec::Initialize(MemoryArena& arena, size_t size) {
  type_count = size;
  types = memory_arena_push_type_count(&arena, DimensionType, size);
  memset(types, 0, sizeof(DimensionType) * size);
}

void DimensionCodec::ParseType(MemoryArena& arena, nbt::TagCompound& nbt, DimensionType& type) {
  ProcessDimensionFlag(type, nbt, POLY_STR("piglin_safe"), DimensionFlag_PiglinSafe);
  ProcessDimensionFlag(type, nbt, POLY_STR("natural"), DimensionFlag_Natural);
  ProcessDimensionFlag(type, nbt, POLY_STR("respawn_anchor_works"), DimensionFlag_RespawnAnchor);
  ProcessDimensionFlag(type, nbt, POLY_STR("has_skylight"), DimensionFlag_HasSkylight);
  ProcessDimensionFlag(type, nbt, POLY_STR("bed_works"), DimensionFlag_BedWorks);
  ProcessDimensionFlag(type, nbt, POLY_STR("has_raids"), DimensionFlag_HasRaids);
  ProcessDimensionFlag(type, nbt, POLY_STR("ultrawarm"), DimensionFlag_Ultrawarm);
  ProcessDimensionFlag(type, nbt, POLY_STR("has_ceiling"), DimensionFlag_HasCeiling);

  ProcessDimensionInt(type, nbt, POLY_STR("min_y"), &type.min_y);
  ProcessDimensionInt(type, nbt, POLY_STR("height"), &type.height);
  ProcessDimensionInt(type, nbt, POLY_STR("logical_height"), &type.logical_height);

  ProcessDimensionFloat(type, nbt, POLY_STR("ambient_light"), &type.ambient_light);
  ProcessDimensionDouble(type, nbt, POLY_STR("coordinate_scale"), &type.coordinate_scale);

  ProcessDimensionLong(type, nbt, POLY_STR("fixed_time"), &type.fixed_time);

  // TODO: effects, infiniburn
}

void DimensionCodec::ParseDefaultType(MemoryArena& arena, size_t index) {
  if (index >= 4) {
    fprintf(stderr, "Failed to receive dimension data for non-core dimension.\n");
    exit(1);
  }

  static const DimensionType kCoreTypes[] = {
      DimensionType{POLY_STR("minecraft:overworld"), String(), String(), 0, 58, -64, 384, 384, 0.0f, 1.0f, 0},
      DimensionType{POLY_STR("minecraft:overworld_caves"), String(), String(), 1, 186, -64, 384, 384, 0.0f, 1.0f, 0},
      DimensionType{POLY_STR("minecraft:the_end"), String(), String(), 2, 32, 0, 256, 256, 0.0f, 1.0f, 6000},
      DimensionType{POLY_STR("minecraft:the_nether"), String(), String(), 3, 197, 0, 256, 128, 0.1f, 8.0f, 18000},
  };

  types[index] = kCoreTypes[index];
}

DimensionType* DimensionCodec::GetDimensionTypeById(s32 id) {
  for (size_t i = 0; i < type_count; ++i) {
    DimensionType* type = types + i;

    if (type->id == id) return type;
  }

  return nullptr;
}

DimensionType* DimensionCodec::GetDimensionTypeByName(const String& identifier) {
  for (size_t i = 0; i < type_count; ++i) {
    DimensionType* type = types + i;

    if (poly_strcmp(identifier, type->name) == 0) {
      return type;
    }
  }

  return nullptr;
}

} // namespace world
} // namespace polymer
