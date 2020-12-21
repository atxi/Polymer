#ifndef POLYMER_ZIP_ARCHIVE_H_
#define POLYMER_ZIP_ARCHIVE_H_

#include "miniz.h"

namespace polymer {

struct MemoryArena;

struct ZipArchiveElement {
  char name[512];
};

struct ZipArchive {
  mz_zip_archive archive;

  bool Open(const char* path);
  void Close();

  char* ReadFile(MemoryArena* arena, const char* filename, size_t* size);
  ZipArchiveElement* ListFiles(MemoryArena* arena, const char* search, size_t* count);
};

} // namespace polymer

#endif
