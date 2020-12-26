#ifndef POLYMER_ASSET_LOADER_H_
#define POLYMER_ASSET_LOADER_H_

#include "json.h"
#include "memory.h"
#include "types.h"
#include "zip_archive.h"

namespace polymer {

struct TextureIdElement {
  TextureIdElement* next;

  char name[32];
  u32 value;
};

constexpr size_t kTextureIdBuckets = (1 << 7);
struct TextureIdMap {
  TextureIdElement* elements[kTextureIdBuckets];

  MemoryArena* arena;
  TextureIdElement* free;

  TextureIdMap(MemoryArena* arena);

  void Insert(const char* name, u32 value);
  u32* Find(const char* name);

  TextureIdElement* Allocate();
};

struct FaceTextureElement {
  FaceTextureElement* next;

  char name[32];
  char value[256];
};

constexpr size_t kTextureMapBuckets = (1 << 5);
struct FaceTextureMap {
  FaceTextureElement* elements[kTextureMapBuckets];

  MemoryArena* arena;
  FaceTextureElement* free;

  FaceTextureMap(MemoryArena* arena);

  void Insert(const char* name, size_t namelen, const char* value, size_t valuelen);
  const char* Find(const char* name);

  FaceTextureElement* Allocate();
};

struct ParsedBlockModel {
  json_value_s* root_value;
  json_object_s* root;

  // Grabs the textures from this model and inserts them into the face texture map
  void InsertTextureMap(FaceTextureMap* map);
};

constexpr size_t kMaxTextureImages = 2048;
struct AssetLoader {
  ZipArchive archive = {};
  MemoryArena* arena;
  ArenaSnapshot snapshot = 0;
  FaceTextureMap face_texture_map;
  TextureIdMap texture_id_map;

  size_t model_count = 0;
  ParsedBlockModel* models;

  u8* texture_images[kMaxTextureImages];

  AssetLoader(MemoryArena* arena) : arena(arena), face_texture_map(arena), texture_id_map(arena) {}

  bool OpenArchive(const char* filename);
  void CloseArchive();

  size_t ParseBlockModels();

  void Cleanup();
};

} // namespace polymer

#endif
