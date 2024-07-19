#pragma once

#include <polymer/math.h>

namespace polymer {

using PolymerWindow = void*;

struct MemoryArena;

struct ExtensionRequest {
  const char** extensions;
  u32 extension_count = 0;

  const char** device_extensions;
  u32 device_extension_count = 0;

  const char** validation_layers;
  u32 validation_layer_count = 0;
};

using PlatformGetPlatformName = const char* (*)();

using PlatformWindowCreate = PolymerWindow (*)(int width, int height);
using PlatformWindowCreateSurface = bool (*)(PolymerWindow window, void* surface);
using PlatformWindowGetRect = IntRect (*)(PolymerWindow window);
using PlatformWindowPump = void (*)(PolymerWindow window);

using PlatformGetExtensionRequest = ExtensionRequest (*)();

using PlatformGetAssetStorePath = String (*)(MemoryArena& arena);

using PlatformFolderExists = bool (*)(const char* path);
using PlatformCreateFolder = bool (*)(const char* path);

using PlatformAllocate = u8* (*)(size_t size);
using PlatformFree = void (*)(u8* ptr);

struct Platform {
  PlatformGetPlatformName GetPlatformName;

  PlatformWindowCreate WindowCreate;
  PlatformWindowCreateSurface WindowCreateSurface;
  PlatformWindowGetRect WindowGetRect;
  PlatformWindowPump WindowPump;

  PlatformGetExtensionRequest GetExtensionRequest;

  PlatformGetAssetStorePath GetAssetStorePath;
  PlatformFolderExists FolderExists;
  PlatformCreateFolder CreateFolder;

  PlatformAllocate Allocate;
  PlatformFree Free;
};
extern Platform g_Platform;

} // namespace polymer
