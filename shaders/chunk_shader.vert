#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
  mat4 mvp;
  uint frame;
  float sunlight;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inTexId;
layout(location = 2) in uint inPackedLight;
layout(location = 3) in uint inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) flat out uint fragTexId;
layout(location = 2) out vec4 fragColorMod;

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
  uint packed_anim = inPackedLight >> 24;
  uint animCount = packed_anim & 0x7F;
  uint animRepeat = (packed_anim >> 7) & 1;

  gl_Position = ubo.mvp * vec4(inPosition, 1.0);
  fragTexCoord.x = (inTexCoord >> 5) / 16.0;
  fragTexCoord.y = (inTexCoord & 0x1F) / 16.0;

  // Have animation repeat itself backwards
  uint frame = 0;

  if (animRepeat > 0) {
    frame = ubo.frame % (animCount * 2);
    if (frame >= animCount) {
      frame = animCount - (frame - animCount) - 1;
    }  
  } else {
    frame = ubo.frame % animCount;
  }

  fragTexId = inTexId + frame;
  fragColorMod = vec4(1, 1, 1, 1);

  // TODO: Remove this and sample biome from foliage/grass png
  uint tintindex = (inPackedLight >> 16) & 0xFF;
  uint ao = inPackedLight & 3;
  
  uint skylight_value = (inPackedLight >> 2) & 0x0F;
  uint blocklight_value = ((inPackedLight >> 2) & 0xF0) >> 4;

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
    fragColorMod.rgb *= (1.0 / 0.8);
  }

  float skylight_percent = (float(skylight_value) / 15.0) * ubo.sunlight * 0.85;
  float blocklight_percent = blocklight_value / 15.0;
  float light_intensity = max(blocklight_percent, skylight_percent) * 0.85 + 0.15;

  uint shaded_axis = (inPackedLight >> 11) & 1;

  light_intensity *= 1.0 - (shaded_axis * 0.2);

  float ao_intensity = (0.25 + float(ao) * 0.25);
  fragColorMod.rgb *= (ao_intensity * light_intensity);
}
