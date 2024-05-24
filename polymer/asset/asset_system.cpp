#include <polymer/asset/asset_system.h>

#include <polymer/asset/unihex_font.h>
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

  perm_arena = CreateArena(Megabytes(256 + 64));

  MemoryArena trans_arena = CreateArena(Megabytes(128));
  BlockAssetLoader block_loader(perm_arena, trans_arena);

  if (!block_loader.Load(renderer, archive, blocks_path, registry)) {
    trans_arena.Destroy();
    perm_arena.Destroy();

    return false;
  }

  this->block_assets = block_loader.assets;

  trans_arena.Reset();

  if (!LoadFont(renderer, perm_arena, trans_arena)) {
    this->glyph_page_texture = nullptr;
    fprintf(stderr, "Failed to load fonts.\n");
  }

  archive.Close();
  trans_arena.Destroy();

  return true;
}

bool AssetSystem::LoadFont(render::VulkanRenderer& renderer, MemoryArena& perm_arena, MemoryArena& trans_arena) {
  constexpr size_t kGlyphPageWidth = 256;
  constexpr size_t kGlyphPageHeight = 256;
  constexpr size_t kGlyphPageCount = 256;

  MemoryRevert trans_arena_revert = trans_arena.GetReverter();

  // Create a texture array to store the glyphs
  glyph_page_texture = renderer.CreateTextureArray(kGlyphPageWidth, kGlyphPageHeight, kGlyphPageCount, 1, false);

  if (!glyph_page_texture) {
    return false;
  }

  size_t table_size = kGlyphPageCount * kGlyphPageCount;

  glyph_size_table = memory_arena_push_type_count(&perm_arena, u8, table_size);
  memset(glyph_size_table, 0, table_size);

  UnihexFont font(glyph_size_table, kGlyphPageWidth, kGlyphPageWidth, kGlyphPageCount);

  String font_zip = asset_store->LoadObject(trans_arena, POLY_STR("minecraft/font/unifont.zip"));
  ZipArchive zip = {};

  if (!zip.OpenFromMemory(font_zip)) {
    fprintf(stderr, "AssetSystem: Failed to open 'minecraft/font/unifont.zip' from memory.\n");
    return false;
  }

  size_t zip_file_count;
  ZipArchiveElement* zip_file_elements = zip.ListFiles(&trans_arena, ".hex", &zip_file_count);

  if (zip_file_count == 0) {
    fprintf(stderr, "AssetSystem: Failed to find '*.hex' file in 'minecraft/font/unifont.zip'.");
    return false;
  }

  size_t unifont_size = 0;
  char* unifont_data = zip.ReadFile(&trans_arena, zip_file_elements[0].name, &unifont_size);

  if (!unifont_data) {
    fprintf(stderr, "AssetSystem: Failed to read '%s' in 'minecraft/font/unifont.zip'", zip_file_elements->name);
    return false;
  }

  if (!font.Load(perm_arena, trans_arena, String(unifont_data, unifont_size))) {
    return false;
  }

  render::TextureConfig texture_cfg(false);

  render::TextureArrayPushState glyph_page_push = renderer.BeginTexturePush(*glyph_page_texture);
  for (size_t i = 0; i < kGlyphPageCount; ++i) {
    u8* page_start = font.images + (256 * 256) * i;
    renderer.PushArrayTexture(trans_arena, glyph_page_push, page_start, i, texture_cfg);
  }

  renderer.CommitTexturePush(glyph_page_push);

  return true;
}

} // namespace asset
} // namespace polymer
