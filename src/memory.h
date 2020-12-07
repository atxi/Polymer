#ifndef POLYMER_MEMORY_H_
#define POLYMER_MEMORY_H_

#include "polymer.h"

namespace polymer {

constexpr size_t kilobytes(size_t n) {
  return n * 1024;
}

constexpr size_t megabytes(size_t n) {
  return n * kilobytes(1024);
}

constexpr size_t gigabytes(size_t n) {
  return n * kilobytes(1024);
}

struct MemoryArena {
  u8* base;
  u8* current;
  size_t max_size;

  MemoryArena(u8* memory, size_t max_size);

  u8* allocate(size_t size, size_t alignment = 4);
  void reset();
};

#define memory_arena_push_type(arena, type) (type*)(arena)->allocate(sizeof(type))
#define memory_arena_push_type_count(arena, type, count) (type*)(arena)->allocate(sizeof(type) * count)

struct RingBuffer {
  u32 read_offset;
  u32 write_offset;

  size_t size;
  u8* data;

  RingBuffer(MemoryArena& arena, size_t size);
};

} // namespace polymer

#endif
