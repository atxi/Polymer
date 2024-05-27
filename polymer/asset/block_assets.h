#ifndef POLYMER_ASSET_BLOCK_ASSET_LOADER_
#define POLYMER_ASSET_BLOCK_ASSET_LOADER_

#include <polymer/hashmap.h>
#include <polymer/world/block.h>

namespace polymer {

struct MemoryArena;
struct ZipArchive;

namespace render {

struct VulkanTexture;
struct VulkanRenderer;

} // namespace render

namespace asset {

// This describes the data about a block texture, such as the base texture id and the animation data.
struct BlockTextureDescriptor {
  // This is the first texture id for the texture. `count` textures are allocated in the texture array to store the
  // animation if it's animated.
  u32 base_texture_id;
  // How many images that make up the texture animation.
  // The texture might be repeated depending on the mcmeta frames list. The count will increase and they will be laid
  // out again in texture memory even for repeats.
  u16 count;

  struct {
    u16 animation_time : 15;
    // Minecraft supports interpolated frames where it combines two of the textures depending on the animation time.
    u16 interpolated : 1;
  };

  bool operator==(const BlockTextureDescriptor& other) const {
    return base_texture_id == other.base_texture_id && count == other.count;
  }
};

typedef HashMap<MapStringKey, BlockTextureDescriptor, MapStringHasher> TextureDescriptorMap;

struct BlockAssets {
  TextureDescriptorMap* texture_descriptor_map = nullptr;
  render::VulkanTexture* block_textures = nullptr;
  world::BlockRegistry* block_registry = nullptr;
};

struct BlockAssetLoader {
  MemoryArena& perm_arena;
  MemoryArena& trans_arena;

  BlockAssets* assets;

  BlockAssetLoader(MemoryArena& perm_arena, MemoryArena& trans_arena)
      : perm_arena(perm_arena), trans_arena(trans_arena), assets(nullptr) {}

  bool Load(render::VulkanRenderer& renderer, ZipArchive& archive, const char* blocks_path,
            world::BlockRegistry* registry);
};

} // namespace asset
} // namespace polymer

#endif
