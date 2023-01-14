#include <polymer/render/render_pass.h>

#include <polymer/types.h>

#include <stdio.h>

namespace polymer {
namespace render {

void RenderPass::Destroy(Swapchain& swapchain) {
  for (u32 i = 0; i < swapchain.image_count; i++) {
    vkDestroyFramebuffer(swapchain.device, framebuffers.framebuffers[i], nullptr);
  }
  vkDestroyRenderPass(swapchain.device, render_pass, nullptr);
  valid = false;
}

void RenderPass::CreateSimple(Swapchain& swapchain, VkAttachmentDescription color, VkAttachmentDescription depth) {
  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depth_attachment_ref = {};
  depth_attachment_ref.attachment = 1;
  depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription attachments[] = {color, depth};

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.pDepthStencilAttachment = &depth_attachment_ref;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = polymer_array_count(attachments);
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  Create(swapchain, &render_pass_info);
}

void RenderPass::Create(Swapchain& swapchain, VkRenderPassCreateInfo* create_info) {
  if (vkCreateRenderPass(swapchain.device, create_info, nullptr, &render_pass) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create render pass.\n");
  }

  this->framebuffers = swapchain.CreateFramebuffers(render_pass);
  valid = true;
}

} // namespace render
} // namespace polymer
