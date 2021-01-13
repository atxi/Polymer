#ifndef POLYMER_ASSET_LOADER_H_
#define POLYMER_ASSET_LOADER_H_

#include "block.h"
#include "json.h"
#include "memory.h"
#include "types.h"
#include "zip_archive.h"

namespace polymer {

struct TextureIdElement {
  TextureIdElement* next;

  char name[48];
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
  const char* Find(const char* name, size_t namelen);

  FaceTextureElement* Allocate();
};

struct ParsedBlockModel {
  char filename[1024];
  json_value_s* root_value;
  json_object_s* root;

  // Grabs the textures from this model and inserts them into the face texture map
  void InsertTextureMap(FaceTextureMap* map);
  void InsertElements(BlockModel* model, FaceTextureMap* texture_face_map, TextureIdMap* texture_id_map);
};

struct ParsedBlockState {
  char filename[1024];
  json_value_s* root_value;
  json_object_s* root;
};

struct AssetLoader {
  ZipArchive archive = {};

  MemoryArena* arena;
  MemoryArena* perm_arena;

  ArenaSnapshot snapshot = 0;

  TextureIdMap texture_id_map;

  size_t model_count = 0;
  ParsedBlockModel* models = nullptr;

  size_t state_count = 0;
  ParsedBlockState* states = nullptr;

  size_t final_state_count = 0;
  BlockState* final_states = nullptr;

  size_t block_info_count = 0;
  BlockStateInfo* block_infos = nullptr;

  size_t texture_count = 0;
  u8* texture_images = nullptr;

  char** properties = nullptr;

  AssetLoader(MemoryArena* arena, MemoryArena* perm_arena)
      : arena(arena), perm_arena(perm_arena), texture_id_map(arena) {}
  ~AssetLoader();

  bool Load(const char* jar_path, const char* blocks_path);

  u8* GetTexture(size_t index);

private:
  bool OpenArchive(const char* filename);
  void CloseArchive();

  bool ParseBlocks(const char* blocks_filename);
  u32 GetLastStateId(json_object_s* root);

  size_t LoadTextures();
  size_t ParseBlockModels();
  size_t ParseBlockStates();
  BlockModel LoadModel(const char* path, size_t path_size, FaceTextureMap* texture_face_map,
                       TextureIdMap* texture_id_map);

  void Cleanup();
};

} // namespace polymer

#endif
