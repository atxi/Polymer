#include <polymer/memory.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace polymer {

// TODO: Get rid of these when a real platform layer is created.
void PlatformFree(u8* ptr) {
#ifdef _WIN32
  VirtualFree(ptr, 0, MEM_RELEASE);
#else
  free(ptr);
#endif
}

u8* PlatformAllocate(size_t size) {
#ifdef _WIN32
  u8* result = (u8*)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  u8* result = (u8*)malloc(size);
#endif

  return result;
}

MemoryArena::MemoryArena(u8* memory, size_t max_size) : base(memory), current(memory), max_size(max_size) {}

u8* MemoryArena::Allocate(size_t size, size_t alignment) {
  assert(alignment > 0);

  size_t adj = alignment - 1;
  u8* result = (u8*)(((size_t)this->current + adj) & ~adj);
  this->current = result + size;

  assert(this->current <= this->base + this->max_size);
  if (this->current > this->base + this->max_size) {
    this->current -= size;
    fprintf(stderr, "Failed to allocate. Arena out of space.\n");
    return nullptr;
  }

  return result;
}

void MemoryArena::Reset() {
  this->current = this->base;
}

void MemoryArena::Destroy() {
  PlatformFree(this->base);

  this->base = this->current = nullptr;
  this->max_size = 0;
}

MemoryArena CreateArena(size_t size) {
  MemoryArena result;

  assert(size > 0);

  result.base = result.current = PlatformAllocate(size);
  result.max_size = size;

  assert(result.base);

  return result;
}

u8* AllocateMirroredBuffer(size_t size) {
#ifdef _WIN32
  SYSTEM_INFO sys_info = {0};

  GetSystemInfo(&sys_info);

  size_t granularity = sys_info.dwAllocationGranularity;

  if (((size / granularity) * granularity) != size) {
    fprintf(stderr, "Incorrect size. Must be multiple of %zd\n", granularity);
    return 0;
  }

  HANDLE map_handle =
      CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, (DWORD)size * 2, NULL);

  if (map_handle == NULL) {
    return 0;
  }

  u8* buffer = (u8*)VirtualAlloc(NULL, size * 2, MEM_RESERVE, PAGE_READWRITE);

  if (buffer == NULL) {
    CloseHandle(map_handle);
    return 0;
  }

  VirtualFree(buffer, 0, MEM_RELEASE);

  u8* view = (u8*)MapViewOfFileEx(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, buffer);

  if (view == NULL) {
    u32 error = GetLastError();
    CloseHandle(map_handle);
    return 0;
  }

  u8* mirror_view = (u8*)MapViewOfFileEx(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, buffer + size);

  if (mirror_view == NULL) {
    u32 error = GetLastError();

    UnmapViewOfFile(view);
    CloseHandle(map_handle);
    return 0;
  }

  return view;
#else
  static_assert(0, "Mirrored buffer not implemented");
#endif
}

} // namespace polymer
