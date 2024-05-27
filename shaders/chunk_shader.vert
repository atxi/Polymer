#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
  mat4 mvp;
  vec4 camera;
  float anim_time;
  float sunlight;
  uint alpha_discard;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inTexId;
layout(location = 2) in uint inPackedLight;
layout(location = 3) in uint inTexCoord;
layout(location = 4) in uint inFrametime;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragTexId;
layout(location = 2) out vec4 fragColorMod;
layout(location = 3) flat out uint fragTexIdInterpolate;
layout(location = 4) flat out float interpolate_t;

#define GRASS_TINTINDEX 0
#define LEAF_TINTINDEX 1
#define SPRUCE_LEAF_TINTINDEX 2
#define BIRCH_LEAF_TINTINDEX 3
#define WATER_TINTINDEX 50

#define WATER_TINT vec4(0.247, 0.463, 0.894, 1.0)

#define JUNGLE_GRASS_TINT vec4(0.349, 0.788, 0.235, 1.0)
#define JUNGLE_LEAF_TINT vec4(0.188, 0.733, 0.043, 1.0)

#define TAIGA_GRASS_TINT vec4(0.525, 0.718, 0.514, 1.0)
#define TAIGA_LEAF_TINT vec4(0.408, 0.643, 0.392, 1.0)

#define PLAINS_GRASS_TINT vec4(0.569, 0.741, 0.349, 1.0)
#define PLAINS_LEAF_TINT vec4(0.467, 0.671, 0.184, 1.0)

// Spruce and birch seem to be not affected by biome coloring
#define SPRUCE_LEAF_TINT vec4(0.380, 0.600, 0.380, 1.0)
#define BIRCH_LEAF_TINT vec4(0.502, 0.655, 0.333, 1.0)

void main() {
  uint animCount = inPackedLight >> 24;
  
  gl_Position = ubo.mvp * vec4(inPosition - ubo.camera.xyz, 1.0);
  fragTexCoord.x = (inTexCoord >> 5) / 16.0;
  fragTexCoord.y = (inTexCoord & 0x1F) / 16.0;

  uint frametime = inFrametime & 0x7FFF;
  uint interpolated = inFrametime >> 15;

  float frame_t = ubo.anim_time * (24.0f / frametime);
  uint frame = uint(frame_t) % animCount;

  fragTexId = inTexId + frame;
  fragColorMod = vec4(1, 1, 1, 1);
  fragTexIdInterpolate = 0;
  interpolate_t = 0.0;

  if (interpolated != 0) {
    fragTexIdInterpolate = inTexId + ((frame + 1) % animCount);
    float frame_index = 0;
    interpolate_t = modf(frame_t, frame_index);
  }

  // TODO: Remove this and sample biome from foliage/grass png
  uint tintindex = (inPackedLight >> 16) & 0x7F;
  uint ao = inPackedLight & 3;
  
  uint skylight_value = (inPackedLight >> 2) & 0x3F;
  uint blocklight_value = (inPackedLight >> 8) & 0x3F;

  if (tintindex == GRASS_TINTINDEX) {
    fragColorMod = PLAINS_GRASS_TINT;
  } else if (tintindex == LEAF_TINTINDEX) {
    fragColorMod = PLAINS_LEAF_TINT;
  } else if (tintindex == SPRUCE_LEAF_TINTINDEX) {
    fragColorMod = SPRUCE_LEAF_TINT;
  } else if (tintindex == BIRCH_LEAF_TINTINDEX) {
    fragColorMod = BIRCH_LEAF_TINT;
  } else if (tintindex == WATER_TINTINDEX) {
    fragColorMod = WATER_TINT;
  }

  // Convert back down into correct brightness
  if (tintindex <= WATER_TINTINDEX) {
    fragColorMod.rgb *= (1.0 / 0.9);
  }

  float skylight_percent = (float(skylight_value) / 60.0) * ubo.sunlight * 0.85;
  float blocklight_percent = blocklight_value / 60.0;
  float light_intensity = max(blocklight_percent, skylight_percent) * 0.85 + 0.15;

  uint shaded_axis = (inPackedLight >> 14) & 1;
  uint vertical_face = (inPackedLight >> 15) & 1;

  // Vary shading of vertical faces by difference between camera and the vertex.
  if (vertical_face > 0) {
      float height_difference = (ubo.camera.y - inPosition.y);
      float shading_modifier = max((abs(height_difference) / 15.0), 1.0);
      light_intensity *= max(1.0 - shading_modifier, 0.8);
  }

  light_intensity *= 1.0 - (shaded_axis * 0.2);

  float ao_intensity = (0.25 + float(ao) * 0.25);
  fragColorMod.rgb *= (ao_intensity * light_intensity);
}
