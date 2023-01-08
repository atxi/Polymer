#ifndef POLYMER_ASSET_PARSED_BLOCK_MODEL_H_
#define POLYMER_ASSET_PARSED_BLOCK_MODEL_H_

#include <polymer/hashmap.h>
#include <polymer/math.h>
#include <polymer/types.h>
#include <polymer/world/block.h>

struct json_object_s;

namespace polymer {

struct MemoryArena;

namespace asset {

struct ParsedTextureName {
  char name[64];
  char value[64];

  ParsedTextureName* next;
};

struct ParsedRenderableFace {
  Vector2f uv_from;
  Vector2f uv_to;

  u32 frame_count;
  char texture_name[64];
  size_t texture_name_size;

  struct {
    u32 render : 1;
    u32 transparency : 1;
    u32 cullface : 3;
    u32 render_layer : 3;
    u32 random_flip : 1;
    u32 padding : 7;
    u32 tintindex : 16;
  };
};

struct ParsedBlockElement {
  ParsedRenderableFace faces[6];
  Vector3f from;
  Vector3f to;

  struct {
    u32 occluding : 1;
    u32 shade : 1;
    u32 rescale : 1;
    u32 padding : 29;
  };
};

struct ParsedBlockModel {
public:
  bool Parse(MemoryArena& trans_arena, const char* raw_filename, json_object_s* root);
  String GetParentName(json_object_s* root) const;
  String ResolveTexture(const String& variable);

  ParsedBlockModel* parent;
  bool parsed;
  world::BlockModel model;

  ParsedTextureName* texture_names;

  size_t element_count;
  ParsedBlockElement elements[20];

  char filename[256];

private:
  void ParseTextures(MemoryArena& trans_arena, json_object_s* root);
  void ParseElements(json_object_s* root);
};

} // namespace asset
} // namespace polymer

#endif
