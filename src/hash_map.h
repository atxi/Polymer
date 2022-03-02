#ifndef POLYMER_HASHMAP_H_
#define POLYMER_HASHMAP_H_

#include "memory.h"
#include "types.h"

namespace polymer {

// Buckets must be power of 2
template <typename Key, typename Value, typename Hasher, size_t buckets = (1 << 8)>
struct HashMap {
  struct Element {
    Key key;
    Value value;

    Element* next;
  };

  MemoryArena& arena;
  Element* elements[buckets];
  Element* free = nullptr;
  Hasher hasher;

  HashMap(MemoryArena& arena) : arena(arena) {
    for (size_t i = 0; i < buckets; ++i) {
      elements[i] = nullptr;
    }

    for (size_t i = 0; i < 32; ++i) {
      Element* element = memory_arena_push_type(&arena, Element);
      element->next = free;
      free = element;
    }
  }

  void Insert(const Key& key, const Value& value) {
    u32 bucket = hasher(key) & (buckets - 1);

    Element* element = elements[bucket];
    while (element) {
      if (element->value == value) {
        break;
      }
      element = element->next;
    }

    if (element == nullptr) {
      element = Allocate();

      element->key = key;

      element->next = elements[bucket];
      elements[bucket] = element;
    }

    element->value = value;
  }

  Value* Remove(const Key& key) {
    u32 bucket = hasher(key) & (buckets - 1);
    Element* element = elements[bucket];
    Element* previous = nullptr;

    while (element) {
      if (element->key == key) {
        if (previous) {
          previous->next = element->next;
        } else {
          elements[bucket] = element->next;
        }

        return &element->value;
      }

      previous = element;
      element = element->next;
    }

    return nullptr;
  }

  Value* Find(const Key& key) {
    u32 bucket = hasher(key) & (buckets - 1);
    Element* element = elements[bucket];

    while (element) {
      if (element->key == key) {
        return &element->value;
      }

      element = element->next;
    }

    return nullptr;
  }

  void Clear() {
    for (size_t i = 0; i < buckets; ++i) {
      Element* element = elements[i];

      while (element) {
        Element* next = element->next;

        element->next = free;
        free = element;

        element = next;
      }

      elements[i] = nullptr;
    }
  }

private:
  Element* Allocate() {
    Element* result = nullptr;

    if (free) {
      result = free;
      free = free->next;
    } else {
      result = memory_arena_push_type(&arena, Element);
    }

    result->next = nullptr;
    return result;
  }
};

struct MapStringKey {
  String key;

  MapStringKey(const String& str) : key(str) {}
  MapStringKey(const char* str, size_t size) {
    key = poly_string(str, size);
  }

  bool operator==(const MapStringKey& other) {
    return poly_strcmp(key, other.key) == 0;
  }
};

struct MapStringHasher {
  inline u32 operator()(const MapStringKey& key) {
    return Hash(key.key);
  }

  inline u32 Hash(const String& str) {
    u32 hash = 5381;

    for (size_t i = 0; i < str.size; ++i) {
      char c = str.data[i];
      hash = hash * 33 ^ c;
    }

    return hash;
  }
};

} // namespace polymer

#endif
