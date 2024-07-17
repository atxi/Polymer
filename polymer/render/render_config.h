#ifndef POLYMER_RENDER_RENDER_CONFIG_H_
#define POLYMER_RENDER_RENDER_CONFIG_H_

#include <polymer/render/vulkan.h>

namespace polymer {
namespace render {

struct RenderConfig {
  VkPresentModeKHR desired_present_mode;
};

} // namespace render
} // namespace polymer

#endif
