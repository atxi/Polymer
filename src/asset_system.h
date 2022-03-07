#ifndef POLYMER_ASSET_SYSTEM_H_
#define POLYMER_ASSET_SYSTEM_H_

#include "block.h"
#include "memory.h"
#include "types.h"

namespace polymer {
namespace render {

struct VulkanRenderer;

} // namespace render

// TODO: Create block id range mappings for namespace name lookups so things like fluid renderers can tell if it's a
// fluid id.
struct AssetSystem {
  BlockRegistry block_registry;

  MemoryArena arena;

  AssetSystem();

  bool Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path);
};

} // namespace polymer

#endif
