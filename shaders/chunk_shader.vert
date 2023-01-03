#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
  mat4 mvp;
  uint frame;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inTexId;
layout(location = 2) in uint inTintIndex;
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
  uint animCount = (inTintIndex >> 8) & 0x7F;
  uint animRepeat = (inTintIndex >> 15) & 1;

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
  uint tintindex = inTintIndex & 0xFF;
  uint ao = (inTintIndex >> 16) & 3;
  uint light_val = (inTintIndex >> 18);

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

  float ao_intensity = (0.25 + float(ao) * 0.25);
  float light_intensity = (float(light_val) / 15.0) * 0.85 + 0.15;

  fragColorMod.rgb *= (ao_intensity * light_intensity);
}
