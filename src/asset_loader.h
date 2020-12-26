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

  void Insert(const char* name, const char* value);
  const char* Find(const char* name);

  FaceTextureElement* Allocate();
};

constexpr size_t kMaxTextureImages = 2048;
struct AssetLoader {
  ZipArchive archive = {};
  MemoryArena* arena;
  ArenaSnapshot snapshot = 0;
  FaceTextureMap face_texture_map;
  TextureIdMap texture_id_map;

  // Stores the parsed BlockModel json objects. Allocated from an arena based on the number of files found in the zip.
  json_object_s** json_models = nullptr;
  u8* texture_images[kMaxTextureImages];

  AssetLoader(MemoryArena* arena) : arena(arena), face_texture_map(arena), texture_id_map(arena) {}

  bool OpenArchive(const char* filename);
  void CloseArchive();

  size_t ParseBlockModels();
};

} // namespace polymer

#endif
