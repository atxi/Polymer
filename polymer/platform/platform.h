#pragma once

#include <polymer/math.h>
#include <polymer/render/vulkan.h>

namespace polymer {

using PolymerWindow = void*;

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
using PlatformWindowCreateSurface = bool (*)(PolymerWindow window, VkSurfaceKHR* surface);
using PlatformWindowGetRect = IntRect (*)(PolymerWindow window);
using PlatformWindowPump = void (*)(PolymerWindow window);

using PlatformGetExtensionRequest = ExtensionRequest (*)();

struct Platform {
  PlatformGetPlatformName GetPlatformName;

  PlatformWindowCreate WindowCreate;
  PlatformWindowCreateSurface WindowCreateSurface;
  PlatformWindowGetRect WindowGetRect;
  PlatformWindowPump WindowPump;

  PlatformGetExtensionRequest GetExtensionRequest;
};

} // namespace polymer
