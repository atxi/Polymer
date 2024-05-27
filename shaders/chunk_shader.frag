#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
  mat4 mvp;
  vec4 camera;
  uint frame;
  float sunlight;
  uint alpha_discard;
} ubo;

layout(binding = 1) uniform sampler2DArray texSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in uint fragTexId;
layout(location = 2) in vec4 fragColorMod;
layout(location = 3) flat in uint fragTexIdInterpolate;
layout(location = 4) flat in float interpolate_t;

layout(location = 0) out vec4 outColor;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// TODO: Move to post-processing and figure out the actual post effects used.
const float saturation = 1.0;
const float brightness = 1.0;

void main() {
  vec4 diffuse = texture(texSampler, vec3(fragTexCoord, fragTexId));
  if (fragTexIdInterpolate != 0xFFFFFFFF) {
    vec4 next_diffuse = texture(texSampler, vec3(fragTexCoord, fragTexIdInterpolate));
    diffuse = mix(diffuse, next_diffuse, interpolate_t);
  }
  
  outColor = diffuse * fragColorMod;

  if (ubo.alpha_discard > 0 && outColor.a <= 0.6) {
    discard;
  }

  vec3 hsv = rgb2hsv(outColor.xyz);

  hsv.y *= saturation;
  hsv.z *= brightness;

  outColor.xyz = hsv2rgb(hsv);
}
