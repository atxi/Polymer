#ifndef POLYMER_MEMORY_H_
#define POLYMER_MEMORY_H_

#include <polymer/platform/platform.h>
#include <polymer/types.h>

#include <new>
#include <stdio.h>
#include <string.h>
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

template <typename T>
struct MemoryPool {
  struct Element {
    T data;

    Element* next;
  };

  constexpr static size_t kPageSize = 4096;

  Element* freelist = nullptr;

  T* Allocate() {
    if (!freelist) {
      // This is how many pages are allocated every time the freelist is empty. This is done to try to fit many
      // allocations into each platform alloc call for small T.
      constexpr size_t kPagesPerAllocCount =
          sizeof(Element) >= kPageSize ? ((sizeof(Element) + kPageSize) / kPageSize) : (kPageSize / sizeof(Element));

      size_t alloc_size = kPagesPerAllocCount * kPageSize;
      u8* data = g_Platform.Allocate(alloc_size);
      u8* data_end = data + alloc_size;

      if (!data) {
        fprintf(stderr, "Failed to allocate data for memory pool (%u).", (u32)sizeof(T));
        return nullptr;
      }

      while (data + sizeof(Element) < data_end) {
        Element* element = (Element*)data;

        element->next = freelist;
        freelist = element;

        data += sizeof(Element);
      }
    }

    if (!freelist) return nullptr;

    Element* result = freelist;

    freelist = freelist->next;

    memset(&result->data, 0, sizeof(T));

    return (T*)result;
  }

  void Free(T* data) {
    // This cast can happen because data is the first member of Element.
    Element* element = (Element*)data;

    element->next = freelist;
    freelist = element;
  }
};

} // namespace polymer

#endif
