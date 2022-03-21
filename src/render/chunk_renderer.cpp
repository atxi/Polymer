#include "chunk_renderer.h"

#include "render.h"

#include <stdio.h>

#pragma warning(disable : 26812) // disable unscoped enum warning

namespace polymer {
namespace render {

void BlockRenderer::CreateRenderPass(VkDevice device, VkFormat swap_format) {
  VkAttachmentDescription color_attachment = {};

  color_attachment.format = swap_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depth_attachment = {};
  depth_attachment.format = VK_FORMAT_D32_SFLOAT;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  CreateRenderPassType(device, swap_format, &render_pass, color_attachment, depth_attachment);
}

void NoMipRenderer::CreateRenderPass(VkDevice device, VkFormat swap_format) {
  VkAttachmentDescription color_attachment = {};

  color_attachment.format = swap_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depth_attachment = {};
  depth_attachment.format = VK_FORMAT_D32_SFLOAT;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  CreateRenderPassType(device, swap_format, &render_pass, color_attachment, depth_attachment);
}

void AlphaRenderer::CreateRenderPass(VkDevice device, VkFormat swap_format) {
  VkAttachmentDescription color_attachment = {};

  color_attachment.format = swap_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depth_attachment = {};
  depth_attachment.format = VK_FORMAT_D32_SFLOAT;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  CreateRenderPassType(device, swap_format, &render_pass, color_attachment, depth_attachment);
}

void ChunkRenderer::CreateRenderPass(VkDevice device, VkFormat swap_format) {
  block_renderer.CreateRenderPass(device, swap_format);
  nomip_renderer.CreateRenderPass(device, swap_format);
  alpha_renderer.CreateRenderPass(device, swap_format);

  VkSamplerCreateInfo sampler_info = {};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.anisotropyEnable = VK_FALSE;
  sampler_info.maxAnisotropy = 4;
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  sampler_info.unnormalizedCoordinates = VK_FALSE;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.mipLodBias = 0.0f;
  sampler_info.minLod = 0.0f;
  sampler_info.maxLod = 0.0f;

  if (vkCreateSampler(device, &sampler_info, nullptr, &nomip_renderer.sampler) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create texture sampler.\n");
  }
}

void ChunkRenderer::SubmitCommands(VkDevice device, VkQueue graphics_queue, size_t current_frame,
                                   VkSemaphore image_available_semaphore, VkSemaphore render_finished_semaphore,
                                   VkFence frame_fence) {
  VkCommandBuffer block_buffer = block_renderer.command_buffers[current_frame];
  VkCommandBuffer nomip_buffer = nomip_renderer.command_buffers[current_frame];
  VkCommandBuffer alpha_buffer = alpha_renderer.command_buffers[current_frame];

  vkCmdEndRenderPass(block_buffer);

  if (vkEndCommandBuffer(block_buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to record command buffer.\n");
  }

  vkCmdEndRenderPass(nomip_buffer);
  if (vkEndCommandBuffer(nomip_buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to record command buffer.\n");
  }

  vkCmdEndRenderPass(alpha_buffer);

  if (vkEndCommandBuffer(alpha_buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to record alpha command buffer.\n");
  }

  {
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {image_available_semaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = waitSemaphores;
    submit_info.pWaitDstStageMask = waitStages;

    VkCommandBuffer buffers[2] = {block_buffer, nomip_buffer};

    submit_info.commandBufferCount = polymer_array_count(buffers);
    submit_info.pCommandBuffers = buffers;

    VkSemaphore signal_semaphores[] = {block_finished_semaphores[current_frame]};

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(graphics_queue, 1, &submit_info, nullptr) != VK_SUCCESS) {
      fprintf(stderr, "Failed to submit draw command buffer.\n");
    }
  }

  {
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {block_finished_semaphores[current_frame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = waitSemaphores;
    submit_info.pWaitDstStageMask = waitStages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &alpha_buffer;

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished_semaphore;

    vkResetFences(device, 1, &frame_fence);

    if (vkQueueSubmit(graphics_queue, 1, &submit_info, frame_fence) != VK_SUCCESS) {
      fprintf(stderr, "Failed to submit draw command buffer.\n");
    }
  }
}

void ChunkRenderer::CreatePipeline(VkDevice device, VkShaderModule vertex_shader, VkShaderModule frag_shader,
                                   VkExtent2D swap_extent, VkPipelineLayout pipeline_layout) {
  VkPipelineShaderStageCreateInfo vert_shader_create_info = {};
  vert_shader_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_shader_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_shader_create_info.module = vertex_shader;
  vert_shader_create_info.pName = "main";

  VkPipelineShaderStageCreateInfo frag_shader_create_info = {};
  frag_shader_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_shader_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_shader_create_info.module = frag_shader;
  frag_shader_create_info.pName = "main";

  VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_create_info, frag_shader_create_info};

  VkVertexInputBindingDescription binding_description = {};

  binding_description.binding = 0;
  binding_description.stride = sizeof(ChunkVertex);
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribute_descriptions[4];
  attribute_descriptions[0].binding = 0;
  attribute_descriptions[0].location = 0;
  attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute_descriptions[0].offset = offsetof(ChunkVertex, position);

  attribute_descriptions[1].binding = 0;
  attribute_descriptions[1].location = 1;
  attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attribute_descriptions[1].offset = offsetof(ChunkVertex, texcoord);

  attribute_descriptions[2].binding = 0;
  attribute_descriptions[2].location = 2;
  attribute_descriptions[2].format = VK_FORMAT_R32_UINT;
  attribute_descriptions[2].offset = offsetof(ChunkVertex, texture_id);

  attribute_descriptions[3].binding = 0;
  attribute_descriptions[3].location = 3;
  attribute_descriptions[3].format = VK_FORMAT_R32_UINT;
  attribute_descriptions[3].offset = offsetof(ChunkVertex, tint_index);

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
  vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &binding_description;
  vertex_input_info.vertexAttributeDescriptionCount = polymer_array_count(attribute_descriptions);
  vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)swap_extent.width;
  viewport.height = (float)swap_extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swap_extent;

  VkPipelineViewportStateCreateInfo viewport_state = {};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  // rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp = 0.0f;
  rasterizer.depthBiasSlopeFactor = 0.0f;

  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = nullptr;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  blend_attachment.blendEnable = VK_FALSE;
  blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo blend = {};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.logicOpEnable = VK_FALSE;
  blend.logicOp = VK_LOGIC_OP_COPY;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  blend.blendConstants[0] = 0.0f;
  blend.blendConstants[1] = 0.0f;
  blend.blendConstants[2] = 0.0f;
  blend.blendConstants[3] = 0.0f;

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
  depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.minDepthBounds = 0.0f;
  depth_stencil.maxDepthBounds = 1.0f;
  depth_stencil.stencilTestEnable = VK_FALSE;
  depth_stencil.front = {};
  depth_stencil.back = {};

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH};

  VkPipelineDynamicStateCreateInfo dynamic_state = {};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;

  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &blend;
  pipeline_info.pDynamicState = nullptr;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = block_renderer.render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &block_renderer.pipeline) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create graphics pipeline.\n");
  }

  pipeline_info.renderPass = nomip_renderer.render_pass;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &nomip_renderer.pipeline) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create graphics pipeline.\n");
  }

  depth_stencil.depthWriteEnable = VK_FALSE;

  blend_attachment.blendEnable = VK_TRUE;
  pipeline_info.renderPass = alpha_renderer.render_pass;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &alpha_renderer.pipeline) !=
      VK_SUCCESS) {
    fprintf(stderr, "Failed to create alpha pipeline.\n");
  }
}

void ChunkRenderer::CreateCommandBuffers(VkDevice device, VkCommandPool command_pool) {
  VkCommandBufferAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = polymer_array_count(block_renderer.command_buffers);

  if (vkAllocateCommandBuffers(device, &alloc_info, block_renderer.command_buffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers.\n");
  }

  if (vkAllocateCommandBuffers(device, &alloc_info, nomip_renderer.command_buffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers.\n");
  }

  if (vkAllocateCommandBuffers(device, &alloc_info, alpha_renderer.command_buffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers.\n");
  }
}

void ChunkRenderer::CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool, VkDescriptorSetLayout* layouts,
                                      VkImageView texture_image_view, VkBuffer* uniform_buffers) {
  VkDescriptorSetAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = descriptor_pool;
  alloc_info.descriptorSetCount = kMaxFramesInFlight;
  alloc_info.pSetLayouts = layouts;

  if (vkAllocateDescriptorSets(device, &alloc_info, nomip_renderer.descriptors) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate descriptor sets.");
  }

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    VkDescriptorBufferInfo buffer_info = {};

    buffer_info.buffer = uniform_buffers[i];
    buffer_info.offset = 0;
    buffer_info.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = texture_image_view;
    image_info.sampler = nomip_renderer.sampler;

    VkWriteDescriptorSet descriptor_writes[2] = {};
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = nomip_renderer.descriptors[i];
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = nomip_renderer.descriptors[i];
    descriptor_writes[1].dstBinding = 1;
    descriptor_writes[1].dstArrayElement = 0;
    descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[1].descriptorCount = 1;
    descriptor_writes[1].pImageInfo = &image_info;
    descriptor_writes[1].pBufferInfo = nullptr;
    descriptor_writes[1].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(device, polymer_array_count(descriptor_writes), descriptor_writes, 0, nullptr);
  }
}

bool ChunkRenderer::BeginFrame(VkRenderPassBeginInfo render_pass_info, size_t current_frame, VkPipelineLayout layout,
                               VkDescriptorSet descriptor) {
  VkCommandBuffer block_buffer = block_renderer.command_buffers[current_frame];
  VkCommandBuffer nomip_buffer = nomip_renderer.command_buffers[current_frame];
  VkCommandBuffer alpha_buffer = alpha_renderer.command_buffers[current_frame];

  VkCommandBufferBeginInfo begin_info = {};

  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = nullptr;

  if (vkBeginCommandBuffer(block_buffer, &begin_info) != VK_SUCCESS) {
    fprintf(stderr, "Failed to begin recording command buffer.\n");
    return false;
  }

  if (vkBeginCommandBuffer(nomip_buffer, &begin_info) != VK_SUCCESS) {
    fprintf(stderr, "Failed to begin recording command buffer.\n");
    return false;
  }

  if (vkBeginCommandBuffer(alpha_buffer, &begin_info) != VK_SUCCESS) {
    fprintf(stderr, "Failed to begin recording command buffer.\n");
    return false;
  }

  VkClearValue clears[] = {{0.71f, 0.816f, 1.0f, 1.0f}, {1.0f, 0}};

  render_pass_info.renderPass = block_renderer.render_pass;
  render_pass_info.clearValueCount = polymer_array_count(clears);
  render_pass_info.pClearValues = clears;

  vkCmdBeginRenderPass(block_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  {
    vkCmdBindPipeline(block_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, block_renderer.pipeline);
    vkCmdBindDescriptorSets(block_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
  }

  render_pass_info.renderPass = nomip_renderer.render_pass;
  render_pass_info.clearValueCount = 0;

  vkCmdBeginRenderPass(nomip_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  {
    vkCmdBindPipeline(nomip_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, block_renderer.pipeline);
    vkCmdBindDescriptorSets(nomip_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            nomip_renderer.descriptors + current_frame, 0, nullptr);
  }

  render_pass_info.renderPass = alpha_renderer.render_pass;
  render_pass_info.clearValueCount = 0;

  vkCmdBeginRenderPass(alpha_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  {
    vkCmdBindPipeline(alpha_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, alpha_renderer.pipeline);
    vkCmdBindDescriptorSets(alpha_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
  }

  return true;
}

void ChunkRenderer::Destroy(VkDevice device, VkCommandPool command_pool) {
  vkFreeCommandBuffers(device, command_pool, kMaxFramesInFlight, block_renderer.command_buffers);
  vkFreeCommandBuffers(device, command_pool, kMaxFramesInFlight, nomip_renderer.command_buffers);
  vkFreeCommandBuffers(device, command_pool, kMaxFramesInFlight, alpha_renderer.command_buffers);

  vkDestroySampler(device, nomip_renderer.sampler, nullptr);
  vkDestroyPipeline(device, block_renderer.pipeline, nullptr);
  vkDestroyPipeline(device, nomip_renderer.pipeline, nullptr);
  vkDestroyPipeline(device, alpha_renderer.pipeline, nullptr);
  vkDestroyRenderPass(device, block_renderer.render_pass, nullptr);
  vkDestroyRenderPass(device, nomip_renderer.render_pass, nullptr);
  vkDestroyRenderPass(device, alpha_renderer.render_pass, nullptr);
}

void ChunkRenderer::CreateSyncObjects(VkDevice device) {
  VkSemaphoreCreateInfo semaphore_info = {};

  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (vkCreateSemaphore(device, &semaphore_info, nullptr, block_finished_semaphores + i) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create semaphore.\n");
    }
  }
}

void ChunkRenderer::CleanupSwapchain(VkDevice device) {
  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkDestroySemaphore(device, block_finished_semaphores[i], nullptr);
  }
}

} // namespace render
} // namespace polymer
