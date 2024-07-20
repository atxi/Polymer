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

void RenderPass::CreateSimple(Swapchain& swapchain, VkAttachmentDescription color, VkAttachmentDescription depth,
                              VkAttachmentDescription color_resolve) {
  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depth_attachment_ref = {};
  depth_attachment_ref.attachment = 1;
  depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_attachment_resolve_ref = {};
  color_attachment_resolve_ref.attachment = 2;
  color_attachment_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription attachments[] = {color, depth, color_resolve};

  u32 attachment_count = polymer_array_count(attachments);

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.pDepthStencilAttachment = &depth_attachment_ref;

  if (swapchain.multisample.samples != VK_SAMPLE_COUNT_1_BIT) {
    subpass.pResolveAttachments = &color_attachment_resolve_ref;
  } else {
    attachment_count--;
  }

  VkSubpassDependency dependencies[2] = {};

  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[0].dstAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

  dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].dstSubpass = 0;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].srcAccessMask = 0;
  dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

  VkRenderPassCreateInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = attachment_count;
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 2;
  render_pass_info.pDependencies = dependencies;

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
