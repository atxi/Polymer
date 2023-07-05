#ifndef POLYMER_RENDER_VULKAN_H_
#define POLYMER_RENDER_VULKAN_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <polymer/render/vk_mem_alloc.h>

#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

#endif
