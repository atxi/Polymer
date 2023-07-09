#include "util.h"

#include <polymer/memory.h>
#include <polymer/platform/platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace polymer {

FILE* CreateAndOpenFileImpl(char* filename, const char* mode) {
  char* current = filename;

  while (*current++) {
    char c = *current;
    if (c == '/' || c == '\\') {
      // Null terminate up to the current folder so it gets checked as existing.
      char prev = *(current + 1);
      *(current + 1) = 0;

      if (!g_Platform.FolderExists(filename)) {
        if (!g_Platform.CreateFolder(filename)) {
          fprintf(stderr, "Failed to create folder '%s'.\n", filename);
          *(current + 1) = prev;
          return nullptr;
        }
      }

      // Revert to previous value so it can keep processing.
      *(current + 1) = prev;
    }
  }

  return fopen(filename, mode);
}

FILE* CreateAndOpenFile(String filename, const char* mode) {
  char* mirror = (char*)malloc(filename.size + 1);

  if (!mirror) return nullptr;

  memcpy(mirror, filename.data, filename.size);
  mirror[filename.size] = 0;

  FILE* f = CreateAndOpenFileImpl(mirror, mode);

  free(mirror);

  return f;
}

FILE* CreateAndOpenFile(const char* filename, const char* mode) {
  size_t name_len = strlen(filename);
  char* mirror = (char*)malloc(name_len + 1);

  if (!mirror) return nullptr;

  memcpy(mirror, filename, name_len);
  mirror[name_len] = 0;

  FILE* f = CreateAndOpenFileImpl(mirror, mode);

  free(mirror);

  return f;
}

String ReadEntireFile(const char* filename, MemoryArena* arena) {
  String result = {};
  FILE* f = fopen(filename, "rb");

  if (!f) {
    return result;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(arena, char, size);

  long total_read = 0;
  while (total_read < size) {
    total_read += (long)fread(buffer + total_read, 1, size - total_read, f);
  }

  fclose(f);

  result.data = buffer;
  result.size = size;

  return result;
}

} // namespace polymer
