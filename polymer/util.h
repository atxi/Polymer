#pragma once

#include <polymer/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace polymer {

struct MemoryArena;

String ReadEntireFile(const char* filename, MemoryArena& arena);

// Creates all the necessary folders and opens a FILE handle.
FILE* CreateAndOpenFile(String filename, const char* mode);
// Creates all the necessary folders and opens a FILE handle.
FILE* CreateAndOpenFile(const char* filename, const char* mode);

struct HashSha1 {
  u8 hash[20] = {};

  HashSha1() {}

  HashSha1(const char* hex, size_t len) {
    char temp[3] = {};

    for (size_t i = 0; i < len; i += 2) {
      temp[0] = hex[i];
      temp[1] = hex[i + 1];
      u8 value = (u8)strtol(temp, nullptr, 16);

      hash[i / 2] = value;
    }
  }

  HashSha1(const char* hex) : HashSha1(hex, strlen(hex)) {}

  bool operator==(const HashSha1& other) const {
    return memcmp(hash, other.hash, sizeof(hash)) == 0;
  }

  bool operator!=(const HashSha1& other) const {
    return !(*this == other);
  }

  void ToString(char* out) {
    for (size_t i = 0; i < sizeof(hash); ++i) {
      sprintf(out + i * 2, "%02x", (int)hash[i]);
    }

    out[40] = 0;
  }
};

HashSha1 Sha1(const String& contents);

} // namespace polymer
