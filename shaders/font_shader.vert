#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
  mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in uint inGlyphId;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragSheetIndex;
layout(location = 2) out vec4 fragColorMod;

void main() {
  gl_Position = ubo.mvp * vec4(inPosition, 1.0);
  
  fragSheetIndex = inGlyphId / 256;
  
  uint index_in_sheet = inGlyphId - (fragSheetIndex * 256);
  float glyph_x = (index_in_sheet % 16) / 16.0f;
  float glyph_y = (index_in_sheet / 16) / 16.0f;
  vec2 uv = inUV / 16.0f;
  
  fragTexCoord = vec2(glyph_x, glyph_y) + uv;

  fragColorMod = vec4(1, 1, 1, 1);
}
