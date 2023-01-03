#include "nbt.h"

#include "buffer.h"
#include "memory.h"

#include <stdio.h>
#include <string.h>

namespace polymer {
namespace nbt {

bool ParseTag(RingBuffer& rb, Tag& tag, MemoryArena& arena);

Tag* TagCompound::GetNamedTag(const String& str) {
  for (size_t i = 0; i < ntags; ++i) {
    Tag* tag = tags + i;
    String tag_name(tag->name, tag->name_length);

    if (poly_strcmp(str, tag_name) == 0) {
      return tag;
    }
  }

  return nullptr;
}

bool ReadLengthString(RingBuffer& rb, char** data, size_t* size, MemoryArena& arena) {
  u16 length;

  if (rb.GetReadAmount() < sizeof(length)) {
    return false;
  }

  length = rb.ReadU16();

  *size = length;
  *data = (char*)arena.Allocate(*size);

  String str;
  str.data = *data;
  str.size = *size;

  if (rb.GetReadAmount() < *size) {
    return false;
  }

  rb.ReadRawString(&str, *size);

  return true;
}

bool ParseCompound(RingBuffer& rb, TagCompound& compound, MemoryArena& arena) {
  compound.ntags = 0;

  TagType type = TagType::Unknown;

  while (type != TagType::End) {
    if (rb.GetReadAmount() < 1) {
      return false;
    }

    type = (TagType)rb.ReadU8();

    if (type == TagType::End) {
      break;
    }

    Tag* tag = compound.tags + compound.ntags++;

    tag->tag = NULL;
    tag->type = type;

    if (!ReadLengthString(rb, &tag->name, &tag->name_length, arena)) {
      return false;
    }

    if (!ParseTag(rb, *tag, arena)) {
      return false;
    }
  }

  return true;
}

bool ParseTag(RingBuffer& rb, Tag& tag, MemoryArena& arena) {
  switch (tag.type) {
  case TagType::End: {
  } break;
  case TagType::Byte: {
    TagByte* byte_tag = memory_arena_push_type(&arena, TagByte);

    if (rb.GetReadAmount() < sizeof(u8)) {
      return false;
    }

    byte_tag->data = rb.ReadU8();

    tag.tag = byte_tag;
  } break;
  case TagType::Short: {
    TagShort* short_tag = memory_arena_push_type(&arena, TagShort);

    if (rb.GetReadAmount() < sizeof(u16)) {
      return false;
    }

    short_tag->data = rb.ReadU16();

    tag.tag = short_tag;
  } break;
  case TagType::Int: {
    TagInt* int_tag = memory_arena_push_type(&arena, TagInt);

    if (rb.GetReadAmount() < sizeof(u32)) {
      return false;
    }

    int_tag->data = rb.ReadU32();

    tag.tag = int_tag;
  } break;
  case TagType::Long: {
    TagLong* long_tag = memory_arena_push_type(&arena, TagLong);

    if (rb.GetReadAmount() < sizeof(u64)) {
      return false;
    }

    long_tag->data = rb.ReadU64();

    tag.tag = long_tag;
  } break;
  case TagType::Float: {
    TagFloat* float_tag = memory_arena_push_type(&arena, TagFloat);

    if (rb.GetReadAmount() < sizeof(float)) {
      return false;
    }

    float_tag->data = rb.ReadFloat();

    tag.tag = float_tag;
  } break;
  case TagType::Double: {
    TagDouble* double_tag = memory_arena_push_type(&arena, TagDouble);

    if (rb.GetReadAmount() < sizeof(double)) {
      return false;
    }

    double_tag->data = rb.ReadDouble();

    tag.tag = double_tag;
  } break;
  case TagType::ByteArray: {
    TagByteArray* byte_array_tag = memory_arena_push_type(&arena, TagByteArray);

    byte_array_tag->length = 0;
    byte_array_tag->data = NULL;

    if (rb.GetReadAmount() < sizeof(u32)) {
      return false;
    }

    byte_array_tag->length = rb.ReadU32();

    if (rb.GetReadAmount() < byte_array_tag->length) {
      return false;
    }

    byte_array_tag->data = (s8*)arena.Allocate(byte_array_tag->length * sizeof(u8));

    // Read all of the contained bytes in one read.
    String str;
    str.data = (char*)byte_array_tag->data;
    str.size = byte_array_tag->length;

    rb.ReadRawString(&str, str.size);

    tag.tag = byte_array_tag;
  } break;
  case TagType::String: {
    TagString* string_tag = memory_arena_push_type(&arena, TagString);

    if (!ReadLengthString(rb, &string_tag->data, &string_tag->length, arena)) {
      return false;
    }

    tag.tag = string_tag;
  } break;
  case TagType::List: {
    TagList* list_tag = memory_arena_push_type(&arena, TagList);

    list_tag->length = 0;
    list_tag->tags = NULL;

    if (rb.GetReadAmount() < sizeof(u8) + sizeof(u32)) {
      return false;
    }

    list_tag->type = (TagType)rb.ReadU8();
    list_tag->length = rb.ReadU32();

    if (list_tag->length > 0) {
      // Allocate space for all of the tags.
      list_tag->tags = memory_arena_push_type_count(&arena, Tag, list_tag->length);
    }

    for (size_t i = 0; i < list_tag->length; ++i) {
      Tag* data_tag = list_tag->tags + i;

      data_tag->name = NULL;
      data_tag->name_length = 0;
      data_tag->type = list_tag->type;

      // TODO: This probably shouldn't be called recursively otherwise bad
      // actors could blow out the stack with nested lists.
      if (!ParseTag(rb, *data_tag, arena)) {
        return false;
      }
    }

    tag.tag = list_tag;
  } break;
  case TagType::Compound: {
    TagCompound* compound_tag = memory_arena_push_type(&arena, TagCompound);

    compound_tag->name = NULL;
    compound_tag->name_length = 0;
    compound_tag->ntags = 0;

    // TODO: This probably shouldn't be called recursively otherwise bad
    // actors could blow out the stack with nested lists.
    if (!ParseCompound(rb, *compound_tag, arena)) {
      return false;
    }

    tag.tag = compound_tag;
  } break;
  case TagType::IntArray: {
    TagIntArray* int_array_tag = memory_arena_push_type(&arena, TagIntArray);

    int_array_tag->length = 0;
    int_array_tag->data = NULL;

    if (rb.GetReadAmount() < sizeof(u32)) {
      return false;
    }

    int_array_tag->length = rb.ReadU32();

    int_array_tag->data = memory_arena_push_type_count(&arena, s32, int_array_tag->length);

    for (size_t i = 0; i < int_array_tag->length; ++i) {
      s32* int_data = int_array_tag->data + i;

      if (rb.GetReadAmount() < sizeof(u32)) {
        return false;
      }

      *int_data = rb.ReadU32();
    }

    tag.tag = int_array_tag;
  } break;
  case TagType::LongArray: {
    TagLongArray* long_array_tag = memory_arena_push_type(&arena, TagLongArray);

    long_array_tag->length = 0;
    long_array_tag->data = NULL;

    if (rb.GetReadAmount() < sizeof(u32)) {
      return false;
    }

    long_array_tag->length = rb.ReadU32();

    long_array_tag->data = memory_arena_push_type_count(&arena, s64, long_array_tag->length);

    for (size_t i = 0; i < long_array_tag->length; ++i) {
      s64* long_data = long_array_tag->data + i;

      if (rb.GetReadAmount() < sizeof(s64)) {
        return false;
      }

      *long_data = rb.ReadU64();
    }

    tag.tag = long_array_tag;
  } break;
  default: {
    fprintf(stderr, "Unknown NBT type: %d\n", (int)tag.type);
    return 0;
  }
  }

  return 1;
}

bool Parse(RingBuffer& rb, MemoryArena& arena, TagCompound* result) {
  TagType type = TagType::Unknown;

  if (rb.GetReadAmount() < sizeof(u8)) {
    return false;
  }

  type = (TagType)rb.ReadU8();

  if (type == TagType::End) {
    return true;
  }

  if (type != TagType::Compound) {
    return false;
  }

  if (!ReadLengthString(rb, &result->name, &result->name_length, arena)) {
    return false;
  }

  if (!ParseCompound(rb, *result, arena)) {
    return false;
  }

  return true;
}

} // namespace nbt
} // namespace polymer
