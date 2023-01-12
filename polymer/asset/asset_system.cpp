#include <polymer/asset/asset_system.h>

#include <polymer/hashmap.h>
#include <polymer/json.h>
#include <polymer/render/render.h>

#include <polymer/zip_archive.h>

#include <polymer/stb_image.h>

#include <stdlib.h>

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

bool AssetSystem::Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path,
                       world::BlockRegistry* registry) {
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

  if (!block_loader.Load(renderer, archive, blocks_path, registry)) {
    trans_arena.Destroy();
    perm_arena.Destroy();

    return false;
  }

  this->block_assets = block_loader.assets;

  if (!LoadFont(renderer, perm_arena, trans_arena, archive)) {
    this->glyph_page_texture = nullptr;
    fprintf(stderr, "Failed to load fonts.\n");
  }

  archive.Close();
  trans_arena.Destroy();

  return true;
}

bool AssetSystem::LoadFont(render::VulkanRenderer& renderer, MemoryArena& perm_arena, MemoryArena& trans_arena,
                           ZipArchive& archive) {
  constexpr size_t kTexturePathPrefixSize = sizeof("assets/minecraft/textures/font/unicode_page_") - 1;
  constexpr int kRequiredDimension = 256;
  // Theres 0x00 through 0xFF different unicode pages.
  constexpr int kPageCount = 256;

  // TODO: The font data should be loaded from json instead of hardcoding the path here.
  size_t unicode_page_count = 0;
  ZipArchiveElement* files =
      archive.ListFiles(&trans_arena, "assets/minecraft/textures/font/unicode_page", &unicode_page_count);

  if (unicode_page_count == 0) {
    return false;
  }

  glyph_page_texture = renderer.CreateTextureArray(kRequiredDimension, kRequiredDimension, kPageCount, 1, false);

  if (!glyph_page_texture) {
    return false;
  }

  render::TextureArrayPushState glyph_page_push = renderer.BeginTexturePush(*glyph_page_texture);

  for (size_t i = 0; i < unicode_page_count; ++i) {
    auto snapshot = trans_arena.GetSnapshot();

    size_t size = 0;
    u8* raw_image = (u8*)archive.ReadFile(&trans_arena, files[i].name, &size);

    assert(raw_image);

    int width, height, channels;

    // TODO: Could be loaded directly into the arena with a define
    stbi_uc* image = stbi_load_from_memory(raw_image, (int)size, &width, &height, &channels, STBI_rgb_alpha);
    if (image == nullptr) {
      continue;
    }

    if (width != kRequiredDimension || height != kRequiredDimension) {
      fprintf(stderr, "Error loading font sheet %s with bad dimensions %d, %d\n", files[i].name, width, height);
      stbi_image_free(image);
      continue;
    }

    // Grab the page index based on the filename
    char* page_index_str = files[i].name + kTexturePathPrefixSize;
    long page_index = strtol(page_index_str, nullptr, 16);

    // Pack the u32 expanded image into one channel.
    // TODO: This could be packed further into a single bit to act as a binary alpha mask.
    u8* single_channel_image = trans_arena.Allocate(width * height);
    for (int i = 0; i < width * height * channels; i += channels) {
      u8 out = image[i + channels - 1];
      u8* current = single_channel_image + i / channels;

      *current = out;

      ++current;
    }

    render::TextureConfig cfg(false);

    renderer.PushArrayTexture(trans_arena, glyph_page_push, single_channel_image, page_index, cfg);

    trans_arena.Revert(snapshot);
  }

  renderer.CommitTexturePush(glyph_page_push);

  size_t table_size = 0;
  char* temp_glyph_size_table = archive.ReadFile(&trans_arena, "assets/minecraft/font/glyph_sizes.bin", &table_size);

  glyph_size_table = memory_arena_push_type_count(&perm_arena, u8, table_size);

  memcpy(glyph_size_table, temp_glyph_size_table, table_size);

  return true;
}

} // namespace asset
} // namespace polymer