#ifndef POLYMER_ASSET_SYSTEM_H_
#define POLYMER_ASSET_SYSTEM_H_

#include "../hash_map.h"
#include "../memory.h"
#include "../types.h"
#include "../world/block.h"

#include "block_assets.h"

namespace polymer {
namespace render {

struct VulkanRenderer;
struct TextureArray;

} // namespace render

namespace asset {

// TODO: Create block id range mappings for namespace name lookups so things like fluid renderers can tell if it's a
// fluid id.
struct AssetSystem {
  MemoryArena perm_arena;
  BlockAssets* block_assets = nullptr;

  AssetSystem();

  bool Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path);

  TextureIdRange GetTextureRange(const String& texture_path);
};

} // namespace asset
} // namespace polymer

#endif
