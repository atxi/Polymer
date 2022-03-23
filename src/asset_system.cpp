#include "asset_system.h"
#include "hash_map.h"
#include "json.h"
#include "render/render.h"

#include "zip_archive.h"

#include "stb_image.h"

namespace polymer {

constexpr size_t kTextureSize = 16 * 16 * 4;
constexpr size_t kNamespaceSize = 10; // "minecraft:"

typedef HashMap<MapStringKey, String, MapStringHasher> FaceTextureMap;

struct ParsedBlockModel {
  String filename;
  json_value_s* root_value;
  json_object_s* root;

  // Grabs the textures from this model and inserts them into the face texture map
  void InsertTextureMap(FaceTextureMap* map);
  void InsertElements(BlockModel* model, FaceTextureMap* texture_face_map, TextureIdMap* texture_id_map);
};

typedef HashMap<MapStringKey, ParsedBlockModel*, MapStringHasher> ParsedBlockMap;

struct ParsedBlockState {
  String filename;

  json_value_s* root_value;
  json_object_s* root;
};

struct AssetParser {
  MemoryArena* arena;
  BlockRegistry* registry;

  ZipArchive archive;

  TextureIdMap texture_id_map;
  TextureIdMap* full_texture_id_map = nullptr;
  ParsedBlockMap parsed_block_map;

  size_t model_count;
  ParsedBlockModel* models = nullptr;

  size_t state_count;
  ParsedBlockState* states = nullptr;

  size_t texture_count;
  u8* texture_images;

  char** properties = nullptr;

  AssetParser(MemoryArena* arena, BlockRegistry* registry)
      : arena(arena), registry(registry), model_count(0), parsed_block_map(*arena), texture_id_map(*arena) {}

  size_t ParseBlockModels();
  size_t ParseBlockStates();
  bool ParseBlocks(MemoryArena* perm_arena, const char* blocks_filename);

  size_t LoadTextures();

  void LoadModels();
  BlockModel LoadModel(String path, FaceTextureMap* texture_face_map, TextureIdMap* texture_id_map);

  bool IsTransparentTexture(u32 texture_id);

  inline u8* GetTexture(size_t index) {
    assert(index < texture_count);
    return texture_images + index * kTextureSize;
  }
};

void AssignFaceRenderSettings(RenderableFace* face, const String& texture) {
  if (poly_contains(texture, POLY_STR("water_still"))) {
    face->render_layer = (int)RenderLayer::Alpha;
  } else if (poly_contains(texture, POLY_STR("grass.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("sugar_cane.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("grass_bottom.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("grass_top.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("fern.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("grass_block_top.png"))) {
    face->random_flip = 1;
  }
}

AssetSystem::AssetSystem() {
  block_registry.info_count = 0;
  block_registry.state_count = 0;
}

TextureIdRange AssetSystem::GetTextureRange(const String& texture_path) {
  if (texture_id_map) {
    TextureIdRange* find = texture_id_map->Find(MapStringKey(texture_path));

    if (find) {
      return *find;
    }
  }

  TextureIdRange empty = {};

  return empty;
}

bool AssetSystem::Load(render::VulkanRenderer& renderer, const char* jar_path, const char* blocks_path) {
  MemoryArena trans_arena = CreateArena(Megabytes(128));
  AssetParser asset_parser(&trans_arena, &block_registry);

  arena = CreateArena(Megabytes(128));
  texture_id_map = memory_arena_construct_type(&arena, TextureIdMap, arena);
  asset_parser.full_texture_id_map = texture_id_map;

  if (!asset_parser.archive.Open(jar_path)) {
    trans_arena.Destroy();
    texture_id_map = nullptr;
    arena.Destroy();
    return false;
  }

  if (!asset_parser.ParseBlockModels()) {
    asset_parser.archive.Close();
    trans_arena.Destroy();
    texture_id_map = nullptr;
    arena.Destroy();
    return false;
  }

  if (!asset_parser.ParseBlockStates()) {
    asset_parser.archive.Close();
    trans_arena.Destroy();
    texture_id_map = nullptr;
    arena.Destroy();
    return false;
  }

  if (asset_parser.LoadTextures() == 0) {
    asset_parser.archive.Close();
    trans_arena.Destroy();
    texture_id_map = nullptr;
    arena.Destroy();
    return false;
  }

  if (!asset_parser.ParseBlocks(&this->arena, blocks_path)) {
    asset_parser.archive.Close();
    trans_arena.Destroy();
    texture_id_map = nullptr;
    arena.Destroy();
    return false;
  }

  asset_parser.LoadModels();

  size_t texture_count = asset_parser.texture_count;

  renderer.CreateTexture(16, 16, texture_count);

  render::TexturePushState push_state = renderer.BeginTexturePush(16, texture_count);

  for (size_t i = 0; i < texture_count; ++i) {
    renderer.PushTexture(trans_arena, push_state, asset_parser.GetTexture(i), i);
  }

  renderer.CommitTexturePush(push_state);

  for (size_t i = 0; i < asset_parser.model_count; ++i) {
    free(asset_parser.models[i].root_value);
  }

  asset_parser.archive.Close();
  trans_arena.Destroy();

  for (size_t i = 0; i < block_registry.state_count; ++i) {
    BlockState* state = block_registry.states + i;

    for (size_t j = 0; j < state->model.element_count; ++j) {
      BlockElement* element = state->model.elements + j;

      // This is wrong, just being done to make grass look better for now.
      if (element->rescale && i == 1398) {
        element->to.y = 0.75f;
      }
    }
  }

  return true;
}

inline String GetFilenameBase(const char* filename) {
  size_t size = 0;

  while (true) {
    char c = filename[size];

    if (c == 0 || c == '.') {
      break;
    }

    ++size;
  }

  return String(filename, size);
}

size_t AssetParser::ParseBlockModels() {
  // Amount of characters to skip over to get to the blockmodel asset name
  constexpr size_t kBlockModelAssetSkip = 30;

  ZipArchiveElement* files = archive.ListFiles(arena, "assets/minecraft/models/block", &model_count);

  if (model_count == 0) {
    return 0;
  }

  models = memory_arena_push_type_count(arena, ParsedBlockModel, model_count);

  for (size_t i = 0; i < model_count; ++i) {
    size_t size = 0;
    char* data = archive.ReadFile(arena, files[i].name, &size);

    assert(data);

    char* filename = files[i].name + kBlockModelAssetSkip;

    models[i].filename = GetFilenameBase(filename);
    models[i].root_value = json_parse(data, size);

    assert(models[i].root_value->type == json_type_object);

    models[i].root = json_value_as_object(models[i].root_value);

    parsed_block_map.Insert(MapStringKey(models[i].filename), models + i);
  }

  return model_count;
}

size_t AssetParser::ParseBlockStates() {
  // Amount of characters to skip over to get to the blockstate asset name
  constexpr size_t kBlockStateAssetSkip = 29;

  ZipArchiveElement* state_files = archive.ListFiles(arena, "assets/minecraft/blockstates/", &state_count);

  if (state_count == 0) {
    return 0;
  }

  states = memory_arena_push_type_count(arena, ParsedBlockState, state_count);

  for (size_t i = 0; i < state_count; ++i) {
    size_t file_size;
    char* data = archive.ReadFile(arena, state_files[i].name, &file_size);

    assert(data);

    states[i].root_value = json_parse(data, file_size);
    states[i].root = json_value_as_object(states[i].root_value);
    states[i].filename = String(state_files[i].name + kBlockStateAssetSkip);
  }

  return state_count;
}

size_t AssetParser::LoadTextures() {
  constexpr size_t kTexturePathPrefixSize = 32;
  ZipArchiveElement* texture_files = archive.ListFiles(arena, "assets/minecraft/textures/block/", &texture_count);

  if (texture_count == 0) {
    return 0;
  }

  // TODO: Allocate this better. This should be enough for current versions but it would be better to allocate to handle
  // any amount.
  this->texture_images = memory_arena_push_type_count(arena, u8, kTextureSize * state_count * 4);

  u32 current_texture_id = 0;

  // TODO: Check for mcmeta file to see if the texture should be rendered with custom rendering settings.
  for (u32 i = 0; i < texture_count; ++i) {
    size_t size = 0;
    u8* raw_image = (u8*)archive.ReadFile(arena, texture_files[i].name, &size);
    int width, height, channels;

    // TODO: Could be loaded directly into the arena with a define
    stbi_uc* image = stbi_load_from_memory(raw_image, (int)size, &width, &height, &channels, STBI_rgb_alpha);
    if (image == nullptr) {
      continue;
    }

    if (width % 16 == 0 && height % 16 == 0) {
      String texture_name = poly_string(texture_files[i].name + kTexturePathPrefixSize);

      TextureIdRange range;
      range.base = current_texture_id;
      range.count = height / 16;

      assert(range.count > 0);

      this->texture_id_map.Insert(texture_name, range);

      size_t perm_name_size = texture_name.size + kTexturePathPrefixSize;
      char* perm_name_alloc = (char*)full_texture_id_map->arena.Allocate(perm_name_size);
      memcpy(perm_name_alloc, texture_files[i].name, perm_name_size);

      String full_texture_name(perm_name_alloc, perm_name_size);

      full_texture_id_map->Insert(full_texture_name, range);

      for (u32 j = 0; j < range.count; ++j) {
        u8* destination = texture_images + (current_texture_id * kTextureSize);
        memcpy(destination, image + j * (width * 16 * 4), kTextureSize);

        ++current_texture_id;
      }
    } else {
      printf("Found image %s with dimensions %d, %d instead of 16 multiple.\n", texture_files[i].name, width, height);
    }

    stbi_image_free(image);
  }

  texture_count = current_texture_id;
  return current_texture_id;
}

static u32 GetLastStateId(json_object_s* root) {
  json_object_element_s* last_root_ele = root->start;

  for (size_t i = 0; i < root->length - 1; ++i) {
    last_root_ele = last_root_ele->next;
  }

  json_object_s* block_obj = json_value_as_object(last_root_ele->value);
  json_object_element_s* block_element = block_obj->start;

  while (block_element) {
    if (strncmp(block_element->name->string, "states", block_element->name->string_size) == 0) {
      json_array_s* states = json_value_as_array(block_element->value);
      json_array_element_s* state_array_element = states->start;

      assert(states->length > 0);

      for (size_t i = 0; i < states->length - 1; ++i) {
        state_array_element = state_array_element->next;
      }

      json_object_s* state_obj = json_value_as_object(state_array_element->value);
      json_object_element_s* state_element = state_obj->start;

      while (state_element) {
        if (strncmp(state_element->name->string, "id", state_element->name->string_size) == 0) {
          return (u32)strtol(json_value_as_number(state_element->value)->number, nullptr, 10);
        }

        state_element = state_element->next;
      }
    }
    block_element = block_element->next;
  }

  return 0;
}

bool AssetParser::ParseBlocks(MemoryArena* perm_arena, const char* blocks_filename) {
  FILE* f = fopen(blocks_filename, "r");
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(arena, char, file_size);

  fread(buffer, 1, file_size, f);
  fclose(f);

  json_value_s* root = json_parse(buffer, file_size);
  assert(root->type == json_type_object);

  json_object_s* root_obj = json_value_as_object(root);
  assert(root_obj);
  assert(root_obj->length > 0);

  registry->state_count = (size_t)GetLastStateId(root_obj) + 1;
  assert(registry->state_count > 1);

  // Create a list of pointers to property strings stored in the transient arena
  properties = (char**)arena->Allocate(sizeof(char*) * registry->state_count);
  registry->states = memory_arena_push_type_count(perm_arena, BlockState, registry->state_count);
  registry->infos = (BlockStateInfo*)memory_arena_push_type_count(perm_arena, BlockStateInfo, root_obj->length);

  json_object_element_s* element = root_obj->start;

  size_t block_state_index = 0;

  while (element) {
    json_object_s* block_obj = json_value_as_object(element->value);
    assert(block_obj);

    BlockStateInfo* info = registry->infos + registry->info_count++;
    assert(element->name->string_size < polymer_array_count(info->name));
    memcpy(info->name, element->name->string, element->name->string_size);
    info->name_length = element->name->string_size;

    json_object_element_s* block_element = block_obj->start;
    while (block_element) {
      if (strncmp(block_element->name->string, "states", block_element->name->string_size) == 0) {
        json_array_s* states = json_value_as_array(block_element->value);
        json_array_element_s* state_array_element = states->start;

        while (state_array_element) {
          json_object_s* state_obj = json_value_as_object(state_array_element->value);

          properties[block_state_index] = nullptr;

          json_object_element_s* state_element = state_obj->start;

          u32 id = 0;
          size_t index = 0;

          while (state_element) {
            if (strncmp(state_element->name->string, "id", state_element->name->string_size) == 0) {
              registry->states[block_state_index].info = info;

              long block_id = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              registry->states[block_state_index].id = block_id;

              id = (u32)block_id;
              index = block_state_index;

              ++block_state_index;
            }
            state_element = state_element->next;
          }

          state_element = state_obj->start;
          while (state_element) {
            if (strncmp(state_element->name->string, "properties", state_element->name->string_size) == 0) {
              // Loop over each property and create a single string that matches the format of blockstates in the jar
              json_object_s* property_object = json_value_as_object(state_element->value);
              json_object_element_s* property_element = property_object->start;

              // Realign the arena for the property pointer to be 32-bit aligned.
              char* property = (char*)arena->Allocate(0, 4);
              properties[index] = property;
              size_t property_length = 0;

              while (property_element) {
                json_string_s* property_value = json_value_as_string(property_element->value);

                if (strcmp(property_element->name->string, "waterlogged") == 0) {
                  property_element = property_element->next;
                  continue;
                }

                // Allocate enough for property_name=property_value
                size_t alloc_size = property_element->name->string_size + 1 + property_value->string_size;

                property_length += alloc_size;

                char* p = (char*)arena->Allocate(alloc_size, 1);

                // Allocate space for a comma to separate the properties
                if (property_element != property_object->start) {
                  arena->Allocate(1, 1);
                  p[0] = ',';
                  ++p;
                  ++property_length;
                }

                memcpy(p, property_element->name->string, property_element->name->string_size);
                p[property_element->name->string_size] = '=';

                memcpy(p + property_element->name->string_size + 1, property_value->string,
                       property_value->string_size);

                property_element = property_element->next;
              }

              arena->Allocate(1, 1);
              properties[index][property_length] = 0;
            }
            state_element = state_element->next;
          }

          state_array_element = state_array_element->next;
        }
      }

      block_element = block_element->next;
    }

    element = element->next;
  }

  assert(registry->info_count == root_obj->length);

  free(root);

  return true;
}

void AssetParser::LoadModels() {
  for (size_t i = 0; i < state_count; ++i) {
    json_object_element_s* root_element = states[i].root->start;

    String blockstate_name = states[i].filename;
    blockstate_name.size -= 5;

    while (root_element) {
      if (strncmp("variants", root_element->name->string, root_element->name->string_size) == 0) {
        json_object_s* variant_obj = json_value_as_object(root_element->value);

        for (size_t bid = 0; bid < registry->state_count; ++bid) {
          if (registry->states[bid].model.element_count > 0) {
            continue;
          }

          String state_name(registry->states[bid].info->name + kNamespaceSize,
                            registry->states[bid].info->name_length - kNamespaceSize);

          if (poly_strcmp(state_name, blockstate_name) != 0) {
            continue;
          }

          json_object_element_s* variant_element = variant_obj->start;

          while (variant_element) {
            const char* variant_name = variant_element->name->string;

            if ((variant_element->name->string_size == 0 && properties[bid] == nullptr) ||
                (properties[bid] != nullptr && strcmp(variant_name, properties[bid]) == 0) ||
                variant_element->next == nullptr) {
              json_object_s* state_details = nullptr;

              if (variant_element->value->type == json_type_array) {
                // TODO: Find out why multiple models are listed under one variant type. Just default to first for now.
                state_details = json_value_as_object(json_value_as_array(variant_element->value)->start->value);
              } else {
                state_details = json_value_as_object(variant_element->value);
              }

              json_object_element_s* state_element = state_details->start;

              while (state_element) {
                if (strcmp(state_element->name->string, "model") == 0) {
                  json_string_s* model_name_json = json_value_as_string(state_element->value);

                  // Do a lookup on the model name then store the model in the BlockState.
                  // Model lookup is going to need to be recursive with the root parent data being filled out first then
                  // cascaded down.
                  const size_t kPrefixSize = 16;

                  ArenaSnapshot snapshot = arena->GetSnapshot();
                  FaceTextureMap texture_face_map(*arena);
                  String model_name(model_name_json->string + kPrefixSize, model_name_json->string_size - kPrefixSize);

                  registry->states[bid].model = LoadModel(model_name, &texture_face_map, &texture_id_map);

                  char* props = properties[bid];

                  if (props) {
                    char* level_str = strstr(props, "level=");

                    if (level_str) {
                      level_str += 6;

                      int level = atoi(level_str);

                      assert(level >= 0 && level <= 15);

                      registry->states[bid].leveled = true;
                      registry->states[bid].level = level;
                    }
                  }

                  arena->Revert(snapshot);
                  variant_element = nullptr;
                  break;
                }
                state_element = state_element->next;
              }
            }

            if (variant_element) {
              variant_element = variant_element->next;
            }
          }
        }
      }

      root_element = root_element->next;
    }
  }
}

bool AssetParser::IsTransparentTexture(u32 texture_id) {
  u8* start = texture_images + texture_id * kTextureSize;

  // Loop through texture looking for alpha that isn't fully opaque.
  for (size_t i = 0; i < kTextureSize; i += 4) {
    if (start[i + 3] != 0xFF) {
      return true;
    }
  }

  return false;
}

BlockModel AssetParser::LoadModel(String path, FaceTextureMap* texture_face_map, TextureIdMap* texture_id_map) {
  BlockModel result = {};
  ParsedBlockModel** find = parsed_block_map.Find(path);

  if (find == nullptr) {
    return result;
  }

  ParsedBlockModel* parsed_model = *find;

  parsed_model->InsertTextureMap(texture_face_map);
  parsed_model->InsertElements(&result, texture_face_map, texture_id_map);

  json_object_element_s* root_element = parsed_model->root->start;
  while (root_element) {
    if (strcmp(root_element->name->string, "parent") == 0) {
      size_t prefix_size = 6;

      json_string_s* parent_name = json_value_as_string(root_element->value);

      for (size_t i = 0; i < parent_name->string_size; ++i) {
        if (parent_name->string[i] == ':') {
          prefix_size = 16;
          break;
        }
      }

      String parent_string(parent_name->string + prefix_size, parent_name->string_size - prefix_size);

      BlockModel parent = LoadModel(parent_string, texture_face_map, texture_id_map);
      for (size_t i = 0; i < parent.element_count; ++i) {
        result.elements[result.element_count++] = parent.elements[i];

        assert(result.element_count < polymer_array_count(result.elements));
      }
    }

    root_element = root_element->next;
  }

  // TODO: This should be removed once the meta files are processed
  bool is_prismarine = poly_strstr(path, "prismarine").data != nullptr;

  bool is_leaves = poly_strstr(path, "leaves").data != nullptr;
  bool is_spruce = false;
  bool is_birch = false;

  if (is_leaves) {
    // Spruce and birch have hardcoded coloring so they go into their own tintindex.
    is_spruce = poly_strstr(path, "spruce").data != nullptr;
    is_birch = poly_strstr(path, "birch").data != nullptr;
  }

  for (size_t i = 0; i < result.element_count; ++i) {
    BlockElement* element = result.elements + i;

    element->occluding = element->from == Vector3f(0, 0, 0) && element->to == Vector3f(1, 1, 1);

    for (size_t j = 0; j < 6; ++j) {
      element->faces[j].transparency = IsTransparentTexture(element->faces[j].texture_id);

      if (is_prismarine) {
        element->faces[j].frame_count = 1;
      }

      if (is_leaves) {
        element->faces[j].tintindex = 1;

        if (is_spruce) {
          element->faces[j].tintindex = 2;
        } else if (is_birch) {
          element->faces[j].tintindex = 3;
        }
      }
    }
  }

  return result;
}

void ParsedBlockModel::InsertTextureMap(FaceTextureMap* map) {
  json_object_element_s* root_element = root->start;

  while (root_element) {
    if (strcmp(root_element->name->string, "textures") == 0) {
      json_object_element_s* texture_element = json_value_as_object(root_element->value)->start;

      while (texture_element) {
        json_string_s* value_string = json_value_as_string(texture_element->value);

        MapStringKey key(poly_string(texture_element->name->string, texture_element->name->string_size));
        String value = poly_string(value_string->string, value_string->string_size);

        map->Insert(key, value);

        texture_element = texture_element->next;
      }
      break;
    }

    root_element = root_element->next;
  }
}

s32 ParseFaceName(const String& str) {
  const char* facename = str.data;
  s32 face_index = 0;

  if (poly_strcmp(str, POLY_STR("down")) == 0 || poly_strcmp(str, POLY_STR("bottom")) == 0) {
    face_index = 0;
  } else if (poly_strcmp(str, POLY_STR("up")) == 0 || poly_strcmp(str, POLY_STR("top")) == 0) {
    face_index = 1;
  } else if (poly_strcmp(str, POLY_STR("north")) == 0) {
    face_index = 2;
  } else if (poly_strcmp(str, POLY_STR("south")) == 0) {
    face_index = 3;
  } else if (poly_strcmp(str, POLY_STR("west")) == 0) {
    face_index = 4;
  } else if (poly_strcmp(str, POLY_STR("east")) == 0) {
    face_index = 5;
  }

  return face_index;
}

s32 ParseFaceName(const char* facename) {
  return ParseFaceName(poly_string(facename));
}

struct JsonVectorParser {
  json_object_element_s* element;
  json_array_element_s* array_element;

  JsonVectorParser(json_object_element_s* element) : element(element) {
    array_element = json_value_as_array(element->value)->start;
  }

  Vector2f Next() {
    Vector2f result;

    result[0] = strtol(json_value_as_number(array_element->value)->number, nullptr, 10) / 16.0f;
    array_element = array_element->next;
    result[1] = strtol(json_value_as_number(array_element->value)->number, nullptr, 10) / 16.0f;
    array_element = array_element->next;

    return result;
  }

  bool HasNext() {
    return array_element != nullptr && array_element->next != nullptr;
  }
};

void ParsedBlockModel::InsertElements(BlockModel* model, FaceTextureMap* texture_face_map,
                                      TextureIdMap* texture_id_map) {
  json_object_element_s* root_element = root->start;

  while (root_element) {
    if (strcmp(root_element->name->string, "elements") == 0) {
      json_array_s* element_array = json_value_as_array(root_element->value);

      json_array_element_s* element_array_element = element_array->start;
      while (element_array_element) {
        json_object_s* element_obj = json_value_as_object(element_array_element->value);

        model->elements[model->element_count].shade = true;

        json_object_element_s* element_property = element_obj->start;
        while (element_property) {
          const char* property_name = element_property->name->string;

          if (strcmp(property_name, "from") == 0) {
            json_array_element_s* vector_element = json_value_as_array(element_property->value)->start;

            for (int i = 0; i < 3; ++i) {
              model->elements[model->element_count].from[i] =
                  strtol(json_value_as_number(vector_element->value)->number, nullptr, 10) / 16.0f;
              vector_element = vector_element->next;
            }
          } else if (strcmp(property_name, "to") == 0) {
            json_array_element_s* vector_element = json_value_as_array(element_property->value)->start;

            for (int i = 0; i < 3; ++i) {
              model->elements[model->element_count].to[i] =
                  strtol(json_value_as_number(vector_element->value)->number, nullptr, 10) / 16.0f;
              vector_element = vector_element->next;
            }
          } else if (strcmp(property_name, "shade") == 0) {
            model->elements[model->element_count].shade = json_value_is_true(element_property->value);
          } else if (strcmp(property_name, "rotation") == 0) {
            json_object_element_s* rotation_obj_element = json_value_as_object(element_property->value)->start;

            while (rotation_obj_element) {
              if (strcmp(rotation_obj_element->name->string, "rescale") == 0) {
                bool rescale = json_value_is_true(rotation_obj_element->value);

                if (rescale) {
                  model->elements[model->element_count].rescale = 1;
                }
              }

              rotation_obj_element = rotation_obj_element->next;
            }

          } else if (strcmp(property_name, "faces") == 0) {
            json_object_element_s* face_obj_element = json_value_as_object(element_property->value)->start;
            while (face_obj_element) {
              const char* facename = face_obj_element->name->string;

              size_t face_index = ParseFaceName(facename);

              json_object_element_s* face_element = json_value_as_object(face_obj_element->value)->start;
              RenderableFace* face = model->elements[model->element_count].faces + face_index;

              face->uv_from = Vector2f(0, 0);
              face->uv_to = Vector2f(1, 1);
              face->render = true;
              face->tintindex = 0xFFFF;
              face->cullface = 6;
              face->render_layer = 0;

              while (face_element) {
                const char* face_property = face_element->name->string;

                if (strcmp(face_property, "texture") == 0) {
                  json_string_s* texture_str = json_value_as_string(face_element->value);
                  String texture_name = poly_string(texture_str->string, texture_str->string_size);

                  while (texture_name.data[0] == '#') {
                    MapStringKey lookup(texture_name.data + 1, texture_name.size - 1);
                    String* result = texture_face_map->Find(lookup);

                    if (result == nullptr) {
                      return;
                    }

                    texture_name = *result;
                  }

                  size_t prefix_size = poly_contains(texture_name, ':') ? 16 : 6;

                  char lookup[1024];
                  sprintf(lookup, "%.*s.png", (u32)(texture_name.size - prefix_size), texture_name.data + prefix_size);

                  String texture_search(lookup);

                  AssignFaceRenderSettings(face, texture_search);

                  TextureIdRange* texture_id_range = texture_id_map->Find(texture_search);

                  if (texture_id_range) {
                    face->texture_id = texture_id_range->base;
                    face->frame_count = texture_id_range->count;
                  } else {
                    face->texture_id = 0;
                    face->frame_count = 1;
                  }
                } else if (strcmp(face_property, "uv") == 0) {
                  JsonVectorParser vec_parser(face_element);

                  Vector2f uv_from, uv_to;

                  if (vec_parser.HasNext()) {
                    uv_from = vec_parser.Next();
                  }

                  if (vec_parser.HasNext()) {
                    uv_to = vec_parser.Next();
                  }

                  face->uv_from = uv_from;
                  face->uv_to = uv_to;
                } else if (strcmp(face_property, "tintindex") == 0) {
                  face->tintindex = (u32)strtol(json_value_as_number(face_element->value)->number, nullptr, 10);
                } else if (strcmp(face_property, "cullface") == 0) {
                  json_string_s* texture_str = json_value_as_string(face_element->value);
                  String face_str = poly_string(texture_str->string, texture_str->string_size);

                  s32 face_index = ParseFaceName(face_str);

                  face->cullface = face_index;
                }

                face_element = face_element->next;
              }

              face_obj_element = face_obj_element->next;
            }
          }

          element_property = element_property->next;
        }

        ++model->element_count;
        assert(model->element_count < polymer_array_count(model->elements));

        element_array_element = element_array_element->next;
      }
    }
    root_element = root_element->next;
  }
}

} // namespace polymer
