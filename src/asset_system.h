#ifndef POLYMER_ASSET_SYSTEM_H_
#define POLYMER_ASSET_SYSTEM_H_

#include "hash_map.h"
#include "memory.h"
#include "types.h"
#include "world/block.h"

namespace polymer {
namespace render {

struct VulkanRenderer;
struct TextureArray;

} // namespace render

struct TextureIdRange {
  u32 base;
  u32 count;

  bool operator==(const TextureIdRange& other) {
    return base == other.base && count == other.count;
  }
};

typedef HashMap<MapStringKey, TextureIdRange, MapStringHasher> TextureIdMap;

// TODO: Create block id range mappings for namespace name lookups so things like fluid renderers can tell if it's a
// fluid id.
struct AssetSystem {
  TextureIdMap* texture_id_map = nullptr;
  render::TextureArray* block_textures = nullptr;
  world::BlockRegistry block_registry;

  MemoryArena arena;

  AssetSystem();

  bool Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path);

  TextureIdRange GetTextureRange(const String& texture_path);
};

} // namespace polymer

#endif
