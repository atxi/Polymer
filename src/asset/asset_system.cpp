#include "asset_system.h"

#include "../hash_map.h"
#include "../json.h"
#include "../render/render.h"

#include "../zip_archive.h"

#include "../stb_image.h"

namespace polymer {
namespace asset {

AssetSystem::AssetSystem() {}

TextureIdRange AssetSystem::GetTextureRange(const String& texture_path) {
  if (block_assets->texture_id_map) {
    TextureIdRange* find = block_assets->texture_id_map->Find(MapStringKey(texture_path));

    if (find) {
      return *find;
    }
  }

  TextureIdRange empty = {};

  return empty;
}

bool AssetSystem::Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path) {
  ZipArchive archive;

  if (!archive.Open(jar_path)) {
    return false;
  }

  // Destroy any existing perm arena in case this gets called twice.
  if (perm_arena.current) {
    perm_arena.Destroy();
  }

  perm_arena = CreateArena(Megabytes(128));

  MemoryArena trans_arena = CreateArena(Megabytes(128));
  BlockAssetLoader block_loader(perm_arena, trans_arena);

  if (!block_loader.Load(renderer, archive, blocks_path)) {
    trans_arena.Destroy();
    perm_arena.Destroy();

    return false;
  }

  this->block_assets = block_loader.assets;

  archive.Close();
  trans_arena.Destroy();

  return true;
}

} // namespace asset
} // namespace polymer
