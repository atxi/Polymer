#ifndef POLYMER_BITSET_H_
#define POLYMER_BITSET_H_

#include "buffer.h"
#include "memory.h"
#include "types.h"

#include <string.h>

namespace polymer {

struct BitSet {
  u64* data;
  size_t total_bit_count;

  BitSet() : data(nullptr), total_bit_count(0) {}
  BitSet(MemoryArena& arena, size_t total_bit_count) {
    size_t total_slices = total_bit_count / 64;

    if (total_bit_count % 64 != 0) {
      ++total_slices;
    }

    this->data = memory_arena_push_type_count(&arena, u64, total_slices);
    this->total_bit_count = total_bit_count;

    memset(data, 0, sizeof(u64) * total_slices);
  }

  bool Read(MemoryArena& arena, RingBuffer& rb) {
    u64 length = 0;

    if (!rb.ReadVarInt(&length)) return false;

    total_bit_count = 64 * length;
    data = memory_arena_push_type_count(&arena, u64, length);

    for (size_t i = 0; i < length; ++i) {
      if (rb.GetReadAmount() < sizeof(u64)) {
        return false;
      }

      data[i] = rb.ReadU64();
    }

    return true;
  }

  inline bool IsSet(size_t bit_index) const {
    if (bit_index >= total_bit_count) return false;

    size_t data_index = bit_index / 64;
    u64 data_offset = bit_index % 64;

    return data[data_index] & (1LL << data_offset);
  }

  inline void Set(size_t bit_index, bool value) {
    if (bit_index >= total_bit_count) return;

    size_t data_index = bit_index / 64;
    u64 data_offset = bit_index % 64;

    if (value) {
      data[data_index] |= (1LL << data_offset);
    } else {
      data[data_index] &= ~(1LL << data_offset);
    }
  }
};

} // namespace polymer

#endif
