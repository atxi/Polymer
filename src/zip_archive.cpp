#include "zip_archive.h"

#include "memory.h"
#include <cstring>

namespace polymer {

bool ZipArchive::Open(const char* path) {
  mz_zip_zero_struct(&archive);

  return mz_zip_reader_init_file_v2(&archive, path, 0, 0, 0);
}

void ZipArchive::Close() {
  mz_zip_reader_end(&archive);

  mz_zip_zero_struct(&archive);
}

char* ZipArchive::ReadFile(MemoryArena* arena, const char* filename, size_t* size) {
  mz_uint32 index;

  if (mz_zip_reader_locate_file_v2(&archive, filename, nullptr, 0, &index)) {
    mz_zip_archive_file_stat stat;

    if (mz_zip_reader_file_stat(&archive, index, &stat)) {
      size_t buffer_size = (size_t)stat.m_uncomp_size;
      void* buffer = arena->Allocate(buffer_size);

      if (mz_zip_reader_extract_to_mem(&archive, index, buffer, buffer_size, 0)) {
        *size = buffer_size;
        return (char*)buffer;
      }
    }
  }

  return nullptr;
}

ZipArchiveElement* ZipArchive::ListFiles(MemoryArena* arena, const char* search, size_t* count) {
  mz_uint archive_count = mz_zip_reader_get_num_files(&archive);
  mz_zip_archive_file_stat stat;

  if (!mz_zip_reader_file_stat(&archive, 0, &stat)) {
    return nullptr;
  }

  ZipArchiveElement* elements = memory_arena_push_type_count(arena, ZipArchiveElement, 0);

  size_t match_count = 0;

  for (unsigned int i = 0; i < archive_count; ++i) {
    if (!mz_zip_reader_file_stat(&archive, i, &stat))
      continue;
    if (mz_zip_reader_is_file_a_directory(&archive, i))
      continue;

    const char* current = stat.m_filename;

    if (search == nullptr || strstr(current, search) != nullptr) {
      arena->Allocate(sizeof(ZipArchiveElement), 1);

      strcpy(elements[match_count++].name, current);
    }
  }

  *count = match_count;

  return elements;
}

} // namespace polymer
