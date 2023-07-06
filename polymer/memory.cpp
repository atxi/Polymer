#include <polymer/memory.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
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
  size_t pagesize = getpagesize();
  int fd = fileno(tmpfile());
  size_t paged_aligned_size = (size / pagesize) * pagesize;

  assert((size / pagesize) == ((size + pagesize - 1) / pagesize));

  // Resize the file to the requested size so the buffer can be mapped to it.
  if (ftruncate(fd, paged_aligned_size) != 0) {
    fprintf(stderr, "ftruncate() error\n");
    return nullptr;
  }

  // Grab some virtual memory space for the full wrapped buffer.
  u8* buffer = (u8*)mmap(NULL, paged_aligned_size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  // Map the beginning of the virtual memory to the tmpfile.
  mmap(buffer, paged_aligned_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  // Map the wrapping section of the buffer back to the tmpfile.
  mmap(buffer + paged_aligned_size, paged_aligned_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);

  return buffer;
#endif
}

} // namespace polymer
