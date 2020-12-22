#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
	mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in uint inTexId;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragTexId;

void main() {
  gl_Position = ubo.mvp * vec4(inPosition, 1.0);
  fragTexCoord = inTexCoord;
  fragTexId = inTexId;
}
