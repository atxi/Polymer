#include "memory.h"

#include <assert.h>

namespace polymer {

MemoryArena::MemoryArena(u8* memory, size_t max_size) : base(memory), current(memory), max_size(max_size) {}

u8* MemoryArena::allocate(size_t size, size_t alignment) {
  assert(alignment > 0);

  size_t adj = alignment - 1;
  u8* result = (u8*)(((size_t)this->current + adj) & ~adj);
  this->current = result + size;

  assert(this->current <= this->base + this->max_size);

  return result;
}

void MemoryArena::reset() {
  this->current = this->base;
}

RingBuffer::RingBuffer(MemoryArena& arena, size_t size) {
  this->data = arena.allocate(size);
  this->size = size;
  this->read_offset = this->write_offset = 0;
}

} // namespace polymer
