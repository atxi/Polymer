#ifndef POLYMER_NBT_H_
#define POLYMER_NBT_H_

#include <polymer/types.h>

namespace polymer {

struct MemoryArena;
struct RingBuffer;

namespace nbt {

enum class TagType : u8 {
  End = 0,
  Byte,
  Short,
  Int,
  Long,
  Float,
  Double,
  ByteArray,
  String,
  List,
  Compound,
  IntArray,
  LongArray,

  Unknown = 0xFF
};

struct Tag {
  void* tag;
  char* name;
  size_t name_length;
  TagType type;
};

constexpr size_t kMaxTags = 1024;

struct TagCompound {
  Tag tags[kMaxTags];
  size_t ntags;

  char* name;
  size_t name_length;

  Tag* GetNamedTag(const String& str);
};

struct TagByte {
  u8 data;
};

struct TagShort {
  u16 data;
};

struct TagInt {
  u32 data;
};

struct TagLong {
  u64 data;
};

struct TagFloat {
  float data;
};

struct TagDouble {
  double data;
};

struct TagByteArray {
  s8* data;
  size_t length;
};

struct TagString {
  char* data;
  size_t length;
};

struct TagList {
  TagType type;
  size_t length;
  Tag* tags;
};

struct TagIntArray {
  s32* data;
  size_t length;
};

struct TagLongArray {
  s64* data;
  size_t length;
};

bool Parse(RingBuffer& rb, MemoryArena& arena, TagCompound* result);

} // namespace nbt
} // namespace polymer

#endif
