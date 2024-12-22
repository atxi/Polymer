#ifndef POLYMER_RENDER_RENDER_PASS_H_
#define POLYMER_RENDER_RENDER_PASS_H_

#include <polymer/render/vulkan.h>

#include <polymer/render/swapchain.h>

#include <assert.h>

namespace polymer {
namespace render {

struct RenderPass {
  VkRenderPass render_pass;
  FramebufferSet framebuffers;

  bool valid = false;

  void Create(Swapchain& swapchain, VkRenderPassCreateInfo* create_info);
  void CreateSimple(Swapchain& swapchain, VkAttachmentDescription color, VkAttachmentDescription depth,
                    VkAttachmentDescription color_resolve);

  void Destroy(Swapchain& swapchain);

  inline void BeginPass(VkCommandBuffer buffer, VkExtent2D extent, size_t index, VkClearValue* clears,
                        size_t clear_count) {
    assert(index < framebuffers.count);
    assert(valid);

    VkRenderPassBeginInfo render_pass_info = {};

    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.framebuffer = framebuffers.framebuffers[index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.renderPass = render_pass;
    render_pass_info.clearValueCount = (u32)clear_count;
    render_pass_info.pClearValues = clears;

    vkCmdBeginRenderPass(buffer, &render_pass_info, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  }

  inline void EndPass(VkCommandBuffer buffer) {
    vkCmdEndRenderPass(buffer);
  }
};

} // namespace render
} // namespace polymer

#endif
