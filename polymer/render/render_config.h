#ifndef POLYMER_RENDER_RENDER_CONFIG_H_
#define POLYMER_RENDER_RENDER_CONFIG_H_

#include <polymer/render/vulkan.h>
#include <polymer/types.h>

namespace polymer {
namespace render {

struct RenderConfig {
  VkPresentModeKHR desired_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  VkSampleCountFlagBits desired_msaa_samples = VK_SAMPLE_COUNT_4_BIT;

  // This makes the multisampling much better, but has a high performance cost that scales with resolution.
  bool sample_shading = true;

  // Range: [1, 32]. This increases rendering and processing time by a lot.
  u8 view_distance = 16;
};

} // namespace render
} // namespace polymer

#endif
