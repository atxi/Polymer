#include "font_renderer.h"

#include "render.h"

#include <stdio.h>

#pragma warning(disable : 26812) // disable unscoped enum warning

namespace polymer {
namespace render {

static const char* kFontVertShader = "shaders/font_vert.spv";
static const char* kFontFragShader = "shaders/font_frag.spv";

#define PACK_UV(x, y) ((x << 1) | (y > 0))
static inline void PushVertex(FontVertex* mapped_vertices, size_t& vertex_count, const Vector3f& pos, u16 uv_xy,
                              u32 rgba, u16 glyph_id) {
  // Any changes to this function should be checked to see if is still inlined so it doesn't cause huge performance
  // loss.
  FontVertex* vertex = mapped_vertices + vertex_count++;

  // TODO: Probably need to do unicode conversion for everything
  vertex->position = pos;
  vertex->uv_xy = uv_xy;
  vertex->rgba = rgba;
  vertex->glyph_id = glyph_id;
}

// Renders a background for the text by sampling from the first glyph in the first unicode page.
// This glyph has a solid pixel at the top corner, so that uv is used and tinted.
static void PushTextBackground(FontVertex* mapped_vertices, size_t& vertex_count, const Vector3f& pos,
                               const Vector2f& size, const Vector4f& color) {
  float width = size.x;
  float height = size.y;
  float start = 0.0f;
  float end = 0.0f;

  u32 r = (u32)(color.x * 255);
  u32 g = (u32)(color.y * 255);
  u32 b = (u32)(color.z * 255);
  u32 a = (u32)(color.w * 255);

  u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;

  u16 uv = 0;

  PushVertex(mapped_vertices, vertex_count, pos + Vector3f(0, 0, 0), uv, rgba, 0);
  PushVertex(mapped_vertices, vertex_count, pos + Vector3f(0, height, 0), uv, rgba, 0);
  PushVertex(mapped_vertices, vertex_count, pos + Vector3f(width, 0, 0), uv, rgba, 0);

  PushVertex(mapped_vertices, vertex_count, pos + Vector3f(width, 0, 0), uv, rgba, 0);
  PushVertex(mapped_vertices, vertex_count, pos + Vector3f(0, height, 0), uv, rgba, 0);
  PushVertex(mapped_vertices, vertex_count, pos + Vector3f(width, height, 0), uv, rgba, 0);
}

void FontRenderer::RenderBackground(const Vector3f& screen_position, const String& str, const Vector4f& color) {
  FontVertex* mapped_vertices = (FontVertex*)buffer_alloc_info.pMappedData;
  float width = (float)GetTextWidth(str);
  float height = 16;

  PushTextBackground(mapped_vertices, vertex_count, screen_position, Vector2f(width, height), color);
}

void FontRenderer::RenderBackground(const Vector3f& screen_position, const Vector2f& size, const Vector4f& color) {
  FontVertex* mapped_vertices = (FontVertex*)buffer_alloc_info.pMappedData;

  PushTextBackground(mapped_vertices, vertex_count, screen_position, size, color);
}

int FontRenderer::GetTextWidth(const String& str) {
  int width = 0;

  if (str.size == 0) return width;

  for (size_t i = 0; i < str.size; ++i) {
    if (str.data[i] != ' ') {
      u32 glyph_id = str.data[i];
      u8 size_entry = glyph_size_table[glyph_id];

      int start_raw = (size_entry >> 4);
      int end_raw = (size_entry & 0x0F) + 1;

      width += end_raw - start_raw + 2;
    } else {
      width += 6;
    }
  }

  // Cut off the trailing width
  return width - 2;
}

static void TextOutput(FontVertex* mapped_vertices, u8* glyph_size_table, size_t& vertex_count, Vector3f pos,
                       const String& str, u32 rgba) {
  // TODO: Implement the rest such as scaling
  // TODO: Should probably use index buffer
  // TODO: Very inefficient
  for (size_t i = 0; i < str.size; ++i) {
    if (str.data[i] != ' ') {
      u16 glyph_id = str.data[i];
      u8 size_entry = glyph_size_table[glyph_id];

      int start = (size_entry >> 4);
      int end = (size_entry & 0x0F) + 1;

      float width = (float)(end - start);
      constexpr float height = 16;

      PushVertex(mapped_vertices, vertex_count, pos + Vector3f(0, 0, 0), PACK_UV(start, 0), rgba, str.data[i]);
      PushVertex(mapped_vertices, vertex_count, pos + Vector3f(0, height, 0), PACK_UV(start, 1), rgba, str.data[i]);
      PushVertex(mapped_vertices, vertex_count, pos + Vector3f(width, 0, 0), PACK_UV(end, 0), rgba, str.data[i]);

      PushVertex(mapped_vertices, vertex_count, pos + Vector3f(width, 0, 0), PACK_UV(end, 0), rgba, str.data[i]);
      PushVertex(mapped_vertices, vertex_count, pos + Vector3f(0, height, 0), PACK_UV(start, 1), rgba, str.data[i]);
      PushVertex(mapped_vertices, vertex_count, pos + Vector3f(width, height, 0), PACK_UV(end, 1), rgba, str.data[i]);

      pos.x += width + 2;
    } else {
      constexpr float kSpaceSkip = 6;
      pos.x += kSpaceSkip;
    }
  }
}

// This font rendering doesn't match Minecraft's font rendering because it uses ascii.png font multiplied by gui scale.
// This is using the unicode page bitmap font instead.
void FontRenderer::RenderText(const Vector3f& screen_position, const String& str, FontStyleFlags style,
                              const Vector4f& color) {
  FontVertex* mapped_vertices = (FontVertex*)buffer_alloc_info.pMappedData;
  Vector3f position = screen_position;

  u32 r = (u32)(color.x * 255);
  u32 g = (u32)(color.y * 255);
  u32 b = (u32)(color.z * 255);
  u32 a = (u32)(color.w * 255);
  u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;

  constexpr float kHorizontalPadding = 4.0f;
  float width = 0.0f;

  if ((style & FontStyle_Background) || (style & FontStyle_Center)) {
    // Calculate total width and render the background before the font so it blends correctly.
    width = (float)GetTextWidth(str) + (kHorizontalPadding * 2);
  }

  if (style & FontStyle_Center) {
    position.x -= width / 2.0f;
  }

  if (style & FontStyle_Background) {
    PushTextBackground(mapped_vertices, vertex_count, position + Vector3f(-kHorizontalPadding, 0, 0),
                       Vector2f(width, 16), Vector4f(0.2f, 0.2f, 0.2f, 0.5f));
  }

  if (style & FontStyle_DropShadow) {
    // Use 30% of the total color for the drop shadow color and render it offset by (1, 1).
    u32 r = (u32)(color.x * 76);
    u32 g = (u32)(color.y * 76);
    u32 b = (u32)(color.z * 76);

    u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;

    TextOutput(mapped_vertices, glyph_size_table, vertex_count, position + Vector3f(1, 1, 0), str, rgba);
  }

  TextOutput(mapped_vertices, glyph_size_table, vertex_count, position, str, rgba);
}

bool FontRenderPipeline::Create(VkDevice device) {
  VkDescriptorSetLayoutBinding ubo_binding = {};
  ubo_binding.binding = 0;
  ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  ubo_binding.descriptorCount = 1;
  ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

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

void FontRenderPipeline::Cleanup(VkDevice device) {
  vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
}

void FontRenderer::CreateRenderPass(VkDevice device, VkFormat swap_format) {
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
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  CreateRenderPassType(device, swap_format, &render_pass, color_attachment, depth_attachment);
}

void FontRenderer::CreatePipeline(MemoryArena& trans_arena, VkDevice device, VkExtent2D swap_extent) {
  String vert_code = ReadEntireFile(kFontVertShader, &trans_arena);
  String frag_code = ReadEntireFile(kFontFragShader, &trans_arena);

  if (vert_code.size == 0) {
    fprintf(stderr, "Failed to read FontRenderer vertex shader file.\n");
  }

  if (frag_code.size == 0) {
    fprintf(stderr, "Failed to read FontRenderer fragment shader file.\n");
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
  binding_description.stride = sizeof(FontVertex);
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribute_descriptions[4];
  attribute_descriptions[0].binding = 0;
  attribute_descriptions[0].location = 0;
  attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute_descriptions[0].offset = offsetof(FontVertex, position);

  attribute_descriptions[1].binding = 0;
  attribute_descriptions[1].location = 1;
  attribute_descriptions[1].format = VK_FORMAT_R32_UINT;
  attribute_descriptions[1].offset = offsetof(FontVertex, rgba);

  attribute_descriptions[2].binding = 0;
  attribute_descriptions[2].location = 2;
  attribute_descriptions[2].format = VK_FORMAT_R16_UINT;
  attribute_descriptions[2].offset = offsetof(FontVertex, glyph_id);

  attribute_descriptions[3].binding = 0;
  attribute_descriptions[3].location = 3;
  attribute_descriptions[3].format = VK_FORMAT_R16_UINT;
  attribute_descriptions[3].offset = offsetof(FontVertex, uv_xy);

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

  blend_attachment.blendEnable = VK_TRUE;
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
  depth_stencil.depthTestEnable = VK_FALSE;
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
  pipeline_info.layout = pipeline.pipeline_layout;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.basePipelineIndex = -1;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &render_pipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create graphics pipeline.\n");
  }

  vkDestroyShaderModule(device, vertex_shader, nullptr);
  vkDestroyShaderModule(device, frag_shader, nullptr);
}

void FontRenderer::CreateCommandBuffers(VulkanRenderer& renderer, VkDevice device, VkCommandPool command_pool) {
  VkCommandBufferAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = polymer_array_count(command_buffers);

  if (vkAllocateCommandBuffers(device, &alloc_info, command_buffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers.\n");
  }
}

void FontRenderer::CreateDescriptors(VkDevice device, VkDescriptorPool descriptor_pool) {
  VkBufferCreateInfo buffer_info = {};

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = sizeof(FontRenderUBO);
  buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
  alloc_create_info.flags = 0;

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (vmaCreateBuffer(renderer->allocator, &buffer_info, &alloc_create_info, uniform_buffers + i,
                        uniform_allocations + i, nullptr) != VK_SUCCESS) {
      printf("Failed to create FontRenderer uniform buffer.\n");
    }
  }

  VkDescriptorSetLayout layouts[kMaxFramesInFlight];

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    layouts[i] = pipeline.descriptor_layout;
  }

  VkDescriptorSetAllocateInfo alloc_info = {};

  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = descriptor_pool;
  alloc_info.descriptorSetCount = kMaxFramesInFlight;
  alloc_info.pSetLayouts = layouts;

  if (vkAllocateDescriptorSets(device, &alloc_info, descriptors) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate descriptor sets.");
  }

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    VkDescriptorBufferInfo buffer_info = {};

    buffer_info.buffer = uniform_buffers[i];
    buffer_info.offset = 0;
    buffer_info.range = sizeof(FontRenderUBO);

    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = glyph_page_texture->image_view;
    image_info.sampler = glyph_page_texture->sampler;

    VkWriteDescriptorSet descriptor_writes[2] = {};
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].dstSet = descriptors[i];
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pTexelBufferView = nullptr;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = descriptors[i];
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

bool FontRenderer::BeginFrame(VkRenderPassBeginInfo render_pass_info, size_t current_frame) {
  VkPipelineLayout layout = pipeline.pipeline_layout;
  VkDescriptorSet& descriptor = descriptors[current_frame];

  VkCommandBuffer command_buffer = command_buffers[current_frame];

  VkCommandBufferBeginInfo begin_info = {};

  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = nullptr;

  if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
    fprintf(stderr, "Failed to begin recording command buffer.\n");
    return false;
  }

  VkClearValue clears[] = {{0.71f, 0.816f, 1.0f, 1.0f}, {1.0f, 0}};

  render_pass_info.renderPass = render_pass;
  render_pass_info.clearValueCount = 0;

  vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
  }

  this->vertex_count = 0;

  vmaMapMemory(allocator, buffer_alloc, &buffer_alloc_info.pMappedData);

  return true;
}

VkSemaphore FontRenderer::SubmitCommands(VkDevice device, VkQueue graphics_queue, size_t current_frame,
                                         VkSemaphore wait_semaphore) {
  VkCommandBuffer command_buffer = command_buffers[current_frame];

  if (vertex_count > 0) {
    FontRenderUBO ubo;
    void* data = nullptr;

    float width = (float)renderer->swap_extent.width;
    float height = (float)renderer->swap_extent.height;
    ubo.mvp = Orthographic(0, width, 0, height, -1.0f, 1.0f);

    vmaMapMemory(allocator, uniform_allocations[renderer->current_frame], &data);
    memcpy(data, &ubo, sizeof(FontRenderUBO));
    vmaUnmapMemory(allocator, uniform_allocations[renderer->current_frame]);

    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(command_buffer, 0, 1, &buffer, offsets);
    vkCmdDraw(command_buffer, (u32)vertex_count, 1, 0, 0);
  }

  vkCmdEndRenderPass(command_buffer);

  if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to record command buffer.\n");
  }

  {
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {wait_semaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = waitSemaphores;
    submit_info.pWaitDstStageMask = waitStages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &finished_semaphores[current_frame];

    if (vkQueueSubmit(graphics_queue, 1, &submit_info, nullptr) != VK_SUCCESS) {
      fprintf(stderr, "Failed to submit draw command buffer.\n");
    }
  }

  vmaUnmapMemory(allocator, buffer_alloc);

  return finished_semaphores[current_frame];
}

void FontRenderer::Destroy(VkDevice device, VkCommandPool command_pool) {
  vkFreeCommandBuffers(device, command_pool, kMaxFramesInFlight, command_buffers);

  vkDestroyPipeline(device, render_pipeline, nullptr);
  vkDestroyRenderPass(device, render_pass, nullptr);
}

void FontRenderer::CreateSyncObjects(VkDevice device) {
  VkSemaphoreCreateInfo semaphore_info = {};

  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (vkCreateSemaphore(device, &semaphore_info, nullptr, finished_semaphores + i) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create semaphore.\n");
    }
  }
}

void FontRenderer::CleanupSwapchain(VkDevice device) {
  for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
    vkDestroySemaphore(device, finished_semaphores[i], nullptr);
  }

  for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
    vmaDestroyBuffer(renderer->allocator, uniform_buffers[i], uniform_allocations[i]);
  }
}

void FontRenderer::CreateLayoutSet(VulkanRenderer& renderer, VkDevice device) {
  pipeline.Create(device);

  VkBufferCreateInfo buffer_info = {};

  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = kFontRenderMaxCharacters * sizeof(FontVertex) * 6;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_create_info = {};
  alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
  alloc_create_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

  if (vmaCreateBuffer(renderer.allocator, &buffer_info, &alloc_create_info, &buffer, &buffer_alloc,
                      &buffer_alloc_info) != VK_SUCCESS) {
    printf("Failed to create font buffer.\n");
  }

  this->renderer = &renderer;
  this->allocator = renderer.allocator;
}

} // namespace render
} // namespace polymer
