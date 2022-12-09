#ifndef POLYMER_ASSET_BLOCK_ASSET_LOADER_
#define POLYMER_ASSET_BLOCK_ASSET_LOADER_

#include "../hash_map.h"
#include "../world/block.h"

namespace polymer {

struct MemoryArena;
struct ZipArchive;

namespace render {

struct TextureArray;
struct VulkanRenderer;

} // namespace render

namespace asset {

struct TextureIdRange {
  u32 base;
  u32 count;

  bool operator==(const TextureIdRange& other) {
    return base == other.base && count == other.count;
  }
};

typedef HashMap<MapStringKey, TextureIdRange, MapStringHasher> TextureIdMap;

struct BlockAssets {
  TextureIdMap* texture_id_map = nullptr;
  render::TextureArray* block_textures = nullptr;
  world::BlockRegistry* block_registry = nullptr;
};

struct BlockAssetLoader {
  MemoryArena& perm_arena;
  MemoryArena& trans_arena;

  BlockAssets* assets;

  BlockAssetLoader(MemoryArena& perm_arena, MemoryArena& trans_arena)
      : perm_arena(perm_arena), trans_arena(trans_arena), assets(nullptr) {}

  bool Load(render::VulkanRenderer& renderer, ZipArchive& archive, const char* blocks_path, world::BlockRegistry* registry);
};

} // namespace asset
} // namespace polymer

#endif
