#ifndef POLYMER_BUFFER_H_
#define POLYMER_BUFFER_H_

#include "memory.h"
#include "polymer.h"

namespace polymer {

struct sized_string {
  char* str;
  size_t size;
};

// Simple circular buffer where the read and write methods assume there's space to operate
// The only method that checks for read/write cursor wrapping is ReadVarInt.
// This could be simplified greatly by using virtual memory wrapping.
struct RingBuffer {
  size_t read_offset;
  size_t write_offset;

  size_t size;
  u8* data;

  RingBuffer(MemoryArena& arena, size_t size);

  void WriteU8(u8 value);
  void WriteU16(u16 value);
  void WriteU32(u32 value);
  void WriteU64(u64 value);
  void WriteVarInt(u64 value);
  void WriteFloat(float value);
  void WriteDouble(double value);
  void WriteString(sized_string& str);

  u8 ReadU8();
  u16 ReadU16();
  u32 ReadU32();
  u64 ReadU64();
  bool ReadVarInt(u64* value);
  float ReadFloat();
  double ReadDouble();
  size_t ReadString(sized_string* str);
  void ReadRawString(sized_string* str, size_t size);

  size_t GetFreeSize() const;
  size_t GetReadAmount() const;
};

size_t GetVarIntSize(u64 value);

} // namespace polymer

#endif
