#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
  mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inRGBA;
layout(location = 2) in uint inGlyphId;
layout(location = 3) in uint inUV;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragSheetIndex;
layout(location = 2) out vec4 fragColorMod;

void main() {
  gl_Position = ubo.mvp * vec4(inPosition, 1.0);
  
  fragSheetIndex = inGlyphId / 256;
  
  uint index_in_sheet = inGlyphId - (fragSheetIndex * 256);
  float glyph_x = (index_in_sheet % 16) / 16.0;
  float glyph_y = (index_in_sheet / 16) / 16.0;

  uint uv_x = inUV >> 1;
  uint uv_y = (inUV & 1) * 16;
  vec2 uv = vec2(uv_x / 256.0, uv_y / 256.0);
  
  fragTexCoord = vec2(glyph_x, glyph_y) + uv;
  
  uint alpha = (inRGBA >> 24) & 0xFF;
  uint blue = (inRGBA >> 16) & 0xFF;
  uint green = (inRGBA >> 8) & 0xFF;
  uint red = (inRGBA) & 0xFF;

  fragColorMod = vec4(red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0);
}
