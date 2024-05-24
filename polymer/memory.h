#ifndef POLYMER_MEMORY_H_
#define POLYMER_MEMORY_H_

#include <polymer/types.h>

#include <new>
#include <utility>

namespace polymer {

constexpr size_t Kilobytes(size_t n) {
  return n * 1024;
}

constexpr size_t Megabytes(size_t n) {
  return n * Kilobytes(1024);
}

constexpr size_t Gigabytes(size_t n) {
  return n * Megabytes(1024);
}

using ArenaSnapshot = u8*;

struct MemoryRevert {
  struct MemoryArena& arena;
  ArenaSnapshot snapshot;

  inline MemoryRevert(MemoryArena& arena, ArenaSnapshot snapshot) : arena(arena), snapshot(snapshot) {}
  inline ~MemoryRevert();
};

struct MemoryArena {
  u8* base;
  u8* current;
  size_t max_size;

  MemoryArena() : base(nullptr), current(nullptr), max_size(0) {}
  MemoryArena(u8* memory, size_t max_size);

  u8* Allocate(size_t size, size_t alignment = 4);
  void Reset();

  ArenaSnapshot GetSnapshot() {
    return current;
  }

  MemoryRevert GetReverter() {
    return MemoryRevert(*this, GetSnapshot());
  }

  void Revert(ArenaSnapshot snapshot) {
    current = snapshot;
  }

  void Destroy();

  template <typename T, typename... Args>
  T* Construct(Args&&... args) {
    T* result = (T*)Allocate(sizeof(T));
    new (result) T(std::forward<Args>(args)...);
    return result;
  }
};

inline MemoryRevert::~MemoryRevert() {
  arena.Revert(snapshot);
}

#define memory_arena_push_type(arena, type) (type*)(arena)->Allocate(sizeof(type))
#define memory_arena_push_type_count(arena, type, count) (type*)(arena)->Allocate(sizeof(type) * count)

// Allocate virtual pages that mirror the first half
u8* AllocateMirroredBuffer(size_t size);

MemoryArena CreateArena(size_t size);

} // namespace polymer

#endif
