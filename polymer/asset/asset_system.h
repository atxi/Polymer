#ifndef POLYMER_ASSET_SYSTEM_H_
#define POLYMER_ASSET_SYSTEM_H_

#include <polymer/hashmap.h>
#include <polymer/memory.h>
#include <polymer/types.h>
#include <polymer/world/block.h>

#include <polymer/asset/block_assets.h>

namespace polymer {
namespace render {

struct VulkanRenderer;
struct TextureArray;

} // namespace render

namespace asset {

struct AssetSystem {
  MemoryArena perm_arena;
  BlockAssets* block_assets = nullptr;
  render::TextureArray* glyph_page_texture = nullptr;
  u8* glyph_size_table = nullptr;

  AssetSystem();

  bool Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path,
            world::BlockRegistry* registry);

  bool LoadFont(render::VulkanRenderer& renderer, MemoryArena& perm_arena, MemoryArena& trans_arena);

  TextureIdRange GetTextureRange(const String& texture_path);
};

} // namespace asset
} // namespace polymer

#endif
