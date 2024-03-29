#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in uint fragSheetIndex;
layout(location = 2) in vec4 fragColorMod;

layout(location = 0) out vec4 outColor;

void main() {
  float value = texture(texSampler, vec3(fragTexCoord, fragSheetIndex)).r;
  
  outColor = fragColorMod;
  outColor.a *= value;

  if (outColor.a <= 0.1) {
    discard;
  }
}
