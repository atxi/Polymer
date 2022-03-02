#ifndef POLYMER_TYPES_H_
#define POLYMER_TYPES_H_

#include <stdint.h>
#include <string.h>

#define polymer_array_count(arr) (sizeof(arr) / (sizeof(*arr)))
#define POLY_STR(str)                                                                                                  \
  String {                                                                                                             \
    (char*)str, polymer_array_count(str) - 1                                                                           \
  }

namespace polymer {

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

struct String {
  char* data;
  size_t size;

  String() : data(nullptr), size(0) {}
  String(char* data) : data(data), size(strlen(data)) {}
  String(char* data, size_t size) : data(data), size(size) {}
  String(const char* data, size_t size) : data((char*)data), size(size) {}

  bool operator==(const String& other) const {
    return data == other.data && size == other.size;
  }
};

inline s32 poly_strcmp(const String& str1, const String& str2) {
  for (size_t i = 0; i < str1.size && i < str2.size; ++i) {
    if (str1.data[i] < str2.data[i]) {
      return -1;
    } else if (str1.data[i] > str2.data[i]) {
      return 1;
    }
  }

  return str1.size == str2.size ? 0 : -1;
}

inline String poly_string(const char* data, size_t size) {
  String result;

  result.data = (char*)data;
  result.size = size;

  return result;
}

inline String poly_string(const char* strz) {
  return poly_string(strz, strlen(strz));
}

inline String poly_strstr(const String& str, const char* substring) {
  size_t sublen = strlen(substring);

  if (sublen > str.size) {
    return String();
  }

  for (size_t i = 0; i < str.size - sublen; ++i) {
    bool found = true;

    for (size_t j = 0; j < sublen; ++j) {
      if (str.data[i + j] != substring[j]) {
        found = false;
        break;
      }
    }

    if (found) {
      return String(str.data + i, str.size - i);
    }
  }

  return String();
}

inline bool poly_contains(const String& str, char c) {
  for (size_t i = 0; i < str.size; ++i) {
    if (str.data[i] == c) {
      return true;
    }
  }

  return false;
}

} // namespace polymer

#endif
