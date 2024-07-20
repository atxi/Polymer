#ifndef POLYMER_RENDER_RENDER_CONFIG_H_
#define POLYMER_RENDER_RENDER_CONFIG_H_

#include <polymer/render/vulkan.h>

namespace polymer {
namespace render {

struct RenderConfig {
  VkPresentModeKHR desired_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  VkSampleCountFlagBits desired_msaa_samples = VK_SAMPLE_COUNT_4_BIT;
};

} // namespace render
} // namespace polymer

#endif
