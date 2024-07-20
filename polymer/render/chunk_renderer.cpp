#include <polymer/render/chunk_renderer.h>

#include <polymer/render/render.h>
#include <polymer/world/world.h>

#include <algorithm>
#include <stdio.h>

#pragma warning(disable : 26812) // disable unscoped enum warning

namespace polymer {
namespace render {

const char* kRenderLayerNames[kRenderLayerCount] = {"opaque", "flora", "leaf", "alpha"};

static const char* kChunkVertShader = "shaders/chunk_vert.spv";
static const char* kChunkFragShader = "shaders/chunk_frag.spv";

bool ChunkRenderLayout::Create(VkDevice device) {
  VkDescriptorSetLayoutBinding ubo_binding = {};
  ubo_binding.binding = 0;
  ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  ubo_binding.descriptorCount = 1;
  ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding sampler_binding{};
  sampler_binding.binding = 1;
  sampler_binding.descriptorCount = 1;
  sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  sampler_binding.pImmutableSamplers = nullptr;
  sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding layout_bindings[] = {ubo_binding, sampler_binding};

  VkDescriptorSetLayoutCreateInfo layout_create_info = {};
  layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_create_info.bindingCount = polymer_array_count(layout_bindings);
  layout_create_info.pBindings = layout_bindings;

  if (vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr, &descriptor_layout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create descriptor set layout.\n");
    return false;
  }

  VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
  pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.setLayoutCount = 1;
  pipeline_layout_create_info.pSetLayouts = &descriptor_layout;
  pipeline_layout_create_info.pushConstantRangeCount = 0;
  pipeline_layout_create_info.pPushConstantRanges = nullptr;

  if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create pipeline layout.\n");
    return false;
  }

  return true;
}

void ChunkRenderLayout::Shutdown(VkDevice device) {
  vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
}

void ChunkRenderer::CreateSamplers(VkDevice device) {
  {
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
    sampler_info.mipmapMode =
        renderer->swapchain.supports_linear_mipmap ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.mipLodBias = 0.5f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;

    if (vkCreateSampler(device, &sampler_info, nullptr, &flora_sampler) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create texture sampler.\n");
    }
  }

  {
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 0;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode =
        renderer->swapchain.supports_linear_mipmap ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.mipLodBias = 0.5f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = (float)block_textures->mips;

    if (vkCreateSampler(device, &sampler_info, nullptr, &leaf_sampler) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create texture sampler.\n");
    }
  }
}

void ChunkRenderer::CreatePipeline(MemoryArena& trans_arena, VkDevice device, VkExtent2D swap_extent) {
  String vert_code = ReadEntireFile(kChunkVertShader, trans_arena);
  String frag_code = ReadEntireFile(kChunkFragShader, trans_arena);

  if (vert_code.size == 0) {
    fprintf(stderr, "Failed to read ChunkRenderer vertex shader file.\n");
    exit(1);
  }

  if (frag_code.size == 0) {
    fprintf(stderr, "Failed to read ChunkRenderer fragment shader file.\n");
    exit(1);
  }

  VkShaderModule vertex_shader = CreateShaderModule(device, vert_code);
  VkShaderModule frag_shader = CreateShaderModule(device, frag_code);

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

  VkVertexInputAttributeDescription attribute_descriptions[5];
  attribute_descriptions[0].binding = 0;
  attribute_descriptions[0].location = 0;
  attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute_descriptions[0].offset = offsetof(ChunkVertex, position);

  attribute_descriptions[1].binding = 0;
  attribute_descriptions[1].location = 1;
  attribute_descriptions[1].format = VK_FORMAT_R32_UINT;
  attribute_descriptions[1].offset = offsetof(ChunkVertex, texture_id);

  attribute_descriptions[2].binding = 0;
  attribute_descriptions[2].location = 2;
  attribute_descriptions[2].format = VK_FORMAT_R32_UINT;
  attribute_descriptions[2].offset = offsetof(ChunkVertex, packed_light);

  attribute_descriptions[3].binding = 0;
  attribute_descriptions[3].location = 3;
  attribute_descriptions[3].format = VK_FORMAT_R16_UINT;
  attribute_descriptions[3].offset = offsetof(ChunkVertex, packed_uv);

  attribute_descriptions[4].binding = 0;
  attribute_descriptions[4].location = 4;
  attribute_descriptions[4].format = VK_FORMAT_R16_UINT;
  attribute_descriptions[4].offset = offsetof(ChunkVertex, packed_frametime);

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
  multisampling.rasterizationSamples = renderer->swapchain.multisample.samples;
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
  pipeline_info.layout = this->layout.pipeline_layout;
  pipeline_info.renderPass = render_pass->render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create ChunkRenderer pipeline.\n");
  }

  depth_stencil.depthWriteEnable = VK_FALSE;
  blend_attachment.blendEnable = VK_TRUE;

  size_t alpha_index = (size_t)RenderLayer::Alpha;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &alpha_pipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create ChunkRenderer alpha pipeline.\n");
  }

  vkDestroyShaderModule(device, vertex_shader, nullptr);
  vkDestroyShaderModule(device, frag_shader, nullptr);
}

DescriptorSet ChunkRenderLayout::CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool) {
  DescriptorSet descriptors = {};

  VkDescriptorSetLayout layouts[kMaxFramesInFlight];

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    layouts[i] = descriptor_layout;
  }

  VkDescriptorSetAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = descriptor_pool;
  alloc_info.descriptorSetCount = kMaxFramesInFlight;
  alloc_info.pSetLayouts = layouts;

  if (vkAllocateDescriptorSets(device, &alloc_info, descriptors.descriptors) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate descriptor sets.");
  }

  return descriptors;
}

void ChunkRenderer::CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool) {
  CreateSamplers(device);

  opaque_ubo.Create(renderer->allocator, sizeof(ChunkRenderUBO));
  alpha_ubo.Create(renderer->allocator, sizeof(ChunkRenderUBO));

  for (size_t i = 0; i < kRenderLayerCount; ++i) {
    descriptor_sets[i] = layout.CreateDescriptors(device, descriptor_pool);
  }

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    VkDescriptorBufferInfo buffer_info = {};

    buffer_info.buffer = opaque_ubo.uniform_buffers[i];
    buffer_info.offset = 0;
    buffer_info.range = sizeof(ChunkRenderUBO);

    VkDescriptorImageInfo block_image_info = {};
    block_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    block_image_info.imageView = block_textures->image_view;
    block_image_info.sampler = block_textures->sampler;

    VkWriteDescriptorSet descriptor_writes[8] = {};
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = descriptor_sets[(size_t)RenderLayer::Standard].descriptors[i];
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = descriptor_sets[(size_t)RenderLayer::Standard].descriptors[i];
    descriptor_writes[1].dstBinding = 1;
    descriptor_writes[1].dstArrayElement = 0;
    descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[1].descriptorCount = 1;
    descriptor_writes[1].pImageInfo = &block_image_info;
    descriptor_writes[1].pBufferInfo = nullptr;
    descriptor_writes[1].pTexelBufferView = nullptr;

    VkDescriptorImageInfo flora_image_info = {};
    flora_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    flora_image_info.imageView = block_textures->image_view;
    flora_image_info.sampler = flora_sampler;

    descriptor_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[2].dstSet = descriptor_sets[(size_t)RenderLayer::Flora].descriptors[i];
    descriptor_writes[2].dstBinding = 0;
    descriptor_writes[2].dstArrayElement = 0;
    descriptor_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[2].descriptorCount = 1;
    descriptor_writes[2].pBufferInfo = &buffer_info;
    descriptor_writes[2].pImageInfo = nullptr;
    descriptor_writes[2].pTexelBufferView = nullptr;

    descriptor_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[3].dstSet = descriptor_sets[(size_t)RenderLayer::Flora].descriptors[i];
    descriptor_writes[3].dstBinding = 1;
    descriptor_writes[3].dstArrayElement = 0;
    descriptor_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[3].descriptorCount = 1;
    descriptor_writes[3].pImageInfo = &flora_image_info;
    descriptor_writes[3].pBufferInfo = nullptr;
    descriptor_writes[3].pTexelBufferView = nullptr;

    VkDescriptorImageInfo leaf_image_info = {};
    leaf_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    leaf_image_info.imageView = block_textures->image_view;
    leaf_image_info.sampler = leaf_sampler;

    descriptor_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[4].dstSet = descriptor_sets[(size_t)RenderLayer::Leaves].descriptors[i];
    descriptor_writes[4].dstBinding = 0;
    descriptor_writes[4].dstArrayElement = 0;
    descriptor_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[4].descriptorCount = 1;
    descriptor_writes[4].pBufferInfo = &buffer_info;
    descriptor_writes[4].pImageInfo = nullptr;
    descriptor_writes[4].pTexelBufferView = nullptr;

    descriptor_writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[5].dstSet = descriptor_sets[(size_t)RenderLayer::Leaves].descriptors[i];
    descriptor_writes[5].dstBinding = 1;
    descriptor_writes[5].dstArrayElement = 0;
    descriptor_writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[5].descriptorCount = 1;
    descriptor_writes[5].pImageInfo = &leaf_image_info;
    descriptor_writes[5].pBufferInfo = nullptr;
    descriptor_writes[5].pTexelBufferView = nullptr;

    VkDescriptorBufferInfo alpha_buffer_info = {};

    alpha_buffer_info.buffer = alpha_ubo.uniform_buffers[i];
    alpha_buffer_info.offset = 0;
    alpha_buffer_info.range = sizeof(ChunkRenderUBO);

    descriptor_writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[6].dstSet = descriptor_sets[(size_t)RenderLayer::Alpha].descriptors[i];
    descriptor_writes[6].dstBinding = 0;
    descriptor_writes[6].dstArrayElement = 0;
    descriptor_writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[6].descriptorCount = 1;
    descriptor_writes[6].pBufferInfo = &alpha_buffer_info;
    descriptor_writes[6].pImageInfo = nullptr;
    descriptor_writes[6].pTexelBufferView = nullptr;

    descriptor_writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[7].dstSet = descriptor_sets[(size_t)RenderLayer::Alpha].descriptors[i];
    descriptor_writes[7].dstBinding = 1;
    descriptor_writes[7].dstArrayElement = 0;
    descriptor_writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[7].descriptorCount = 1;
    descriptor_writes[7].pImageInfo = &block_image_info;
    descriptor_writes[7].pBufferInfo = nullptr;
    descriptor_writes[7].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(device, polymer_array_count(descriptor_writes), descriptor_writes, 0, nullptr);
  }
}

void ChunkRenderer::Draw(VkCommandBuffer command_buffer, size_t current_frame, world::World& world, Camera& camera,
                         float anim_time, float sunlight) {
  const VkExtent2D& extent = renderer->GetExtent();
  camera.aspect_ratio = (float)extent.width / extent.height;

  render::ChunkRenderUBO ubo;
  void* data = nullptr;

  ubo.mvp = camera.GetProjectionMatrix() * camera.GetViewMatrix();
  ubo.camera = Vector4f(camera.position, 0);
  ubo.anim_time = anim_time;
  ubo.sunlight = sunlight;
  ubo.alpha_discard = true;

  opaque_ubo.Set(current_frame, &ubo, sizeof(ubo));

  ubo.alpha_discard = false;

  alpha_ubo.Set(current_frame, &ubo, sizeof(ubo));

  Frustum frustum = camera.GetViewFrustum();

  VkDeviceSize offsets[] = {0};
  VkDeviceSize offset = {};

#if DISPLAY_PERF_STATS
  stats.Reset();
#endif

  ChunkFrameCommandBuffers& buffers = frame_command_buffers[current_frame];

  VkCommandBufferInheritanceInfo inherit = {};
  inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  inherit.renderPass = render_pass->render_pass;
  inherit.framebuffer = render_pass->framebuffers.framebuffers[renderer->current_image];

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  begin_info.pInheritanceInfo = &inherit;

  for (size_t i = 0; i < kRenderLayerCount; ++i) {
    RenderLayer layer = (RenderLayer)i;
    VkDescriptorSet descriptor = descriptor_sets[i].descriptors[current_frame];

    vkBeginCommandBuffer(buffers.command_buffers[i], &begin_info);

    VkPipeline current_pipeline = layer == RenderLayer::Alpha ? alpha_pipeline : pipeline;

    vkCmdBindPipeline(buffers.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
    vkCmdBindDescriptorSets(buffers.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, this->layout.pipeline_layout,
                            0, 1, &descriptor, 0, nullptr);
  }

  struct AlphaRenderElement {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    u32 index_count;

    float z_dot;
  };

  MemoryRevert trans_revert = renderer->trans_arena->GetReverter();
  AlphaRenderElement* alpha_elements = (AlphaRenderElement*)renderer->trans_arena->Allocate(0, 8);
  size_t alpha_element_count = 0;

  for (s32 chunk_z = 0; chunk_z < (s32)world::kChunkCacheSize; ++chunk_z) {
    for (s32 chunk_x = 0; chunk_x < (s32)world::kChunkCacheSize; ++chunk_x) {
      if (!world.occupy_set.HasChunk(chunk_x, chunk_z)) continue;

      world::ChunkSectionInfo* section_info = &world.chunk_infos[chunk_z][chunk_x];

      if (!section_info->loaded) {
        continue;
      }

      world::ChunkMesh* meshes = world.meshes[chunk_z][chunk_x];

      for (s32 chunk_y = 0; chunk_y < world::kChunkColumnCount; ++chunk_y) {
        world::ChunkMesh* mesh = meshes + chunk_y;

        if ((section_info->bitmask & (1 << chunk_y))) {
          Vector3f chunk_min(section_info->x * 16.0f, chunk_y * 16.0f - 64.0f, section_info->z * 16.0f);
          Vector3f chunk_max(section_info->x * 16.0f + 16.0f, chunk_y * 16.0f - 48.0f, section_info->z * 16.0f + 16.0f);

          if (frustum.Intersects(chunk_min, chunk_max)) {
#if DISPLAY_PERF_STATS
            bool rendered = false;
#endif
            for (s32 i = 0; i < render::kRenderLayerCount; ++i) {
              render::RenderMesh* layer_mesh = &mesh->meshes[i];

              if (layer_mesh->vertex_count > 0) {
                if (i == (s32)RenderLayer::Alpha) {
                  AlphaRenderElement* element = memory_arena_push_type(renderer->trans_arena, AlphaRenderElement);

                  element->vertex_buffer = layer_mesh->vertex_buffer;
                  element->index_buffer = layer_mesh->index_buffer;
                  element->index_count = layer_mesh->index_count;
                  element->z_dot = Vector3f(chunk_x * 16.0f, chunk_y * 16.0f, chunk_z * 16.0f).Dot(camera.GetForward());
                  ++alpha_element_count;
                } else {
                  VkCommandBuffer current_buffer = buffers.command_buffers[i];

                  vkCmdBindVertexBuffers(current_buffer, 0, 1, &layer_mesh->vertex_buffer, offsets);
                  vkCmdBindIndexBuffer(current_buffer, layer_mesh->index_buffer, offset, VK_INDEX_TYPE_UINT16);
                  vkCmdDrawIndexed(current_buffer, layer_mesh->index_count, 1, 0, 0, 0);
                }

#if DISPLAY_PERF_STATS
                stats.vertex_counts[i] += layer_mesh->vertex_count;
                rendered = true;
#endif
              }
            }

#if DISPLAY_PERF_STATS
            if (rendered) {
              ++stats.chunk_render_count;
            }
#endif
          }
        }
      }
    }
  }

  std::sort(alpha_elements, alpha_elements + alpha_element_count,
            [](const AlphaRenderElement& a, const AlphaRenderElement& b) { return a.z_dot > b.z_dot; });

  for (size_t i = 0; i < alpha_element_count; ++i) {
    AlphaRenderElement* element = alpha_elements + i;
    VkCommandBuffer current_buffer = buffers.command_buffers[(size_t)RenderLayer::Alpha];

    vkCmdBindVertexBuffers(current_buffer, 0, 1, &element->vertex_buffer, offsets);
    vkCmdBindIndexBuffer(current_buffer, element->index_buffer, offset, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(current_buffer, element->index_count, 1, 0, 0, 0);
  }

  // Submit each render layer's commands to the primary command buffer.
  // The alpha layer is rendered last because Vulkan guarantees blend and depth tests are done in submission order.
  // No extra synchronization is required.
  for (size_t i = 0; i < kRenderLayerCount; ++i) {
    vkEndCommandBuffer(buffers.command_buffers[i]);

    vkCmdExecuteCommands(command_buffer, 1, buffers.command_buffers + i);
  }
}

void ChunkRenderer::OnSwapchainCreate(MemoryArena& trans_arena, Swapchain& swapchain,
                                      VkDescriptorPool descriptor_pool) {
  CreateDescriptors(swapchain.device, descriptor_pool);
  CreatePipeline(trans_arena, swapchain.device, swapchain.extent);

  VkCommandBufferAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = renderer->command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  alloc_info.commandBufferCount = polymer_array_count(frame_command_buffers[0].command_buffers);

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkAllocateCommandBuffers(swapchain.device, &alloc_info, frame_command_buffers[i].command_buffers);
  }
}

void ChunkRenderer::OnSwapchainDestroy(VkDevice device) {
  vkDestroySampler(device, flora_sampler, nullptr);
  vkDestroySampler(device, leaf_sampler, nullptr);

  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipeline(device, alpha_pipeline, nullptr);

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkFreeCommandBuffers(device, renderer->command_pool, polymer_array_count(frame_command_buffers[0].command_buffers),
                         frame_command_buffers[i].command_buffers);
  }

  opaque_ubo.Destroy();
  alpha_ubo.Destroy();
}

} // namespace render
} // namespace polymer
