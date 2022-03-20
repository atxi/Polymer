#include "dimension.h"
#include "memory.h"
#include "nbt.h"

namespace polymer {

inline void ProcessDimensionFlag(DimensionType* type, nbt::TagCompound& element_compound, String name, u32 flag) {
  nbt::Tag* flag_tag = element_compound.GetNamedTag(name);

  if (flag_tag && flag_tag->type == nbt::TagType::Byte) {
    nbt::TagByte* data_tag = (nbt::TagByte*)flag_tag->tag;

    if (data_tag->data) {
      type->flags |= flag;
    }
  }
}

template <typename DataTag, typename T>
inline void ProcessDimensionData(DimensionType* type, nbt::TagCompound& element_compound, String name,
                                 nbt::TagType tag_type, T* out) {
  nbt::Tag* named_tag = element_compound.GetNamedTag(name);

  if (named_tag && named_tag->type == tag_type) {
    DataTag* data_tag = (DataTag*)named_tag->tag;

    *out = data_tag->data;
  }
}

inline void ProcessDimensionInt(DimensionType* type, nbt::TagCompound& element_compound, String name, int* out) {
  ProcessDimensionData<nbt::TagInt, int>(type, element_compound, name, nbt::TagType::Int, out);
}

inline void ProcessDimensionFloat(DimensionType* type, nbt::TagCompound& element_compound, String name, float* out) {
  ProcessDimensionData<nbt::TagFloat, float>(type, element_compound, name, nbt::TagType::Float, out);
}

inline void ProcessDimensionDouble(DimensionType* type, nbt::TagCompound& element_compound, String name, double* out) {
  ProcessDimensionData<nbt::TagDouble, double>(type, element_compound, name, nbt::TagType::Double, out);
}

inline void ProcessDimensionLong(DimensionType* type, nbt::TagCompound& element_compound, String name, u64* out) {
  ProcessDimensionData<nbt::TagLong, u64>(type, element_compound, name, nbt::TagType::Long, out);
}

void DimensionCodec::Parse(MemoryArena& arena, nbt::TagCompound& nbt) {
  types = nullptr;
  type_count = 0;

  nbt::Tag* named_tag = nbt.GetNamedTag(POLY_STR("minecraft:dimension_type"));

  if (named_tag && named_tag->type == nbt::TagType::Compound) {
    nbt::TagCompound* dimension_nbt = (nbt::TagCompound*)named_tag->tag;
    nbt::Tag* value_tag = dimension_nbt->GetNamedTag(POLY_STR("value"));

    if (value_tag) {
      nbt::TagList* entry_list = (nbt::TagList*)value_tag->tag;

      types = memory_arena_push_type_count(&arena, DimensionType, entry_list->length);
      type_count = entry_list->length;

      memset(types, 0, sizeof(DimensionType) * entry_list->length);

      for (size_t i = 0; i < entry_list->length; ++i) {
        DimensionType* type = types + i;

        nbt::Tag* entry_tag = entry_list->tags + i;

        if (entry_tag->type == nbt::TagType::Compound) {
          nbt::TagCompound* entry_compound = (nbt::TagCompound*)entry_tag->tag;
          nbt::Tag* name_tag = entry_compound->GetNamedTag(POLY_STR("name"));

          if (name_tag && name_tag->type == nbt::TagType::String) {
            nbt::TagString* name_str = (nbt::TagString*)name_tag->tag;

            type->name.data = (char*)arena.Allocate(name_str->length);
            type->name.size = name_str->length;

            memcpy(type->name.data, name_str->data, name_str->length);
          }

          nbt::Tag* id_tag = entry_compound->GetNamedTag(POLY_STR("id"));
          if (id_tag && id_tag->type == nbt::TagType::Int) {
            nbt::TagInt* id_int = (nbt::TagInt*)id_tag->tag;

            type->id = id_int->data;
          }

          nbt::Tag* element_tag = entry_compound->GetNamedTag(POLY_STR("element"));
          if (element_tag && element_tag->type == nbt::TagType::Compound) {
            nbt::TagCompound* element_compound = (nbt::TagCompound*)element_tag->tag;

            ParseType(arena, *element_compound, type);
          }
        }
      }
    }
  }
}

void DimensionCodec::ParseType(MemoryArena& arena, nbt::TagCompound& nbt, DimensionType* type) {
  ProcessDimensionFlag(type, nbt, POLY_STR("piglin_safe"), DimensionFlag_PiglinSafe);
  ProcessDimensionFlag(type, nbt, POLY_STR("natural"), DimensionFlag_Natural);
  ProcessDimensionFlag(type, nbt, POLY_STR("respawn_anchor_works"), DimensionFlag_RespawnAnchor);
  ProcessDimensionFlag(type, nbt, POLY_STR("has_skylight"), DimensionFlag_HasSkylight);
  ProcessDimensionFlag(type, nbt, POLY_STR("bed_works"), DimensionFlag_BedWorks);
  ProcessDimensionFlag(type, nbt, POLY_STR("has_raids"), DimensionFlag_HasRaids);
  ProcessDimensionFlag(type, nbt, POLY_STR("ultrawarm"), DimensionFlag_Ultrawarm);
  ProcessDimensionFlag(type, nbt, POLY_STR("has_ceiling"), DimensionFlag_HasCeiling);

  ProcessDimensionInt(type, nbt, POLY_STR("min_y"), &type->min_y);
  ProcessDimensionInt(type, nbt, POLY_STR("height"), &type->height);
  ProcessDimensionInt(type, nbt, POLY_STR("logical_height"), &type->logical_height);

  ProcessDimensionFloat(type, nbt, POLY_STR("ambient_light"), &type->ambient_light);
  ProcessDimensionDouble(type, nbt, POLY_STR("coordinate_scale"), &type->coordinate_scale);

  ProcessDimensionLong(type, nbt, POLY_STR("fixed_time"), &type->fixed_time);

  // TODO: effects, infiniburn
}

DimensionType* DimensionCodec::GetDimensionType(const String& identifier) {
  for (size_t i = 0; i < type_count; ++i) {
    DimensionType* type = types + i;

    if (poly_strcmp(identifier, type->name) == 0) {
      return type;
    }
  }

  return nullptr;
}

} // namespace polymer
