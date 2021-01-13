#include "asset_loader.h"

#include "stb_image.h"

#include <cstdio>
#include <cstring>

namespace polymer {

constexpr size_t kTextureSize = 16 * 16 * 4;

u32 Hash(const char* str) {
  u32 hash = 5381;
  char c;

  while (c = *str++) {
    hash = hash * 33 ^ c;
  }

  return hash;
}

u32 Hash(const char* str, size_t len) {
  u32 hash = 5381;

  for (size_t i = 0; i < len; ++i) {
    hash = hash * 33 ^ str[i];
  }

  return hash;
}

TextureIdMap::TextureIdMap(MemoryArena* arena) : arena(arena), free(nullptr) {
  for (size_t i = 0; i < kTextureIdBuckets; ++i) {
    elements[i] = nullptr;
  }

  for (size_t i = 0; i < 1024; ++i) {
    TextureIdElement* element = memory_arena_push_type(arena, TextureIdElement);
    element->next = free;
    free = element;
  }
}

TextureIdElement* TextureIdMap::Allocate() {
  TextureIdElement* result = nullptr;

  if (free) {
    result = free;
    free = free->next;
  } else {
    result = memory_arena_push_type(arena, TextureIdElement);
  }

  result->next = nullptr;
  return result;
}

void TextureIdMap::Insert(const char* name, u32 value) {
  u32 bucket = Hash(name) & (kTextureIdBuckets - 1);

  TextureIdElement* element = elements[bucket];
  while (element) {
    if (strcmp(element->name, name) == 0) {
      break;
    }
    element = element->next;
  }

  if (element == nullptr) {
    element = Allocate();

    assert(strlen(name) < polymer_array_count(element->name));

    strcpy(element->name, name);
    element->value = value;

    element->next = elements[bucket];
    elements[bucket] = element;
  }
}

u32* TextureIdMap::Find(const char* name) {
  u32 bucket = Hash(name) & (kTextureIdBuckets - 1);
  TextureIdElement* element = elements[bucket];

  while (element) {
    if (strcmp(element->name, name) == 0) {
      return &element->value;
    }
    element = element->next;
  }

  return nullptr;
}

FaceTextureMap::FaceTextureMap(MemoryArena* arena) : arena(arena), free(nullptr) {
  for (size_t i = 0; i < kTextureMapBuckets; ++i) {
    elements[i] = nullptr;
  }

  for (size_t i = 0; i < 32; ++i) {
    FaceTextureElement* element = memory_arena_push_type(arena, FaceTextureElement);
    element->next = free;
    free = element;
  }
}

FaceTextureElement* FaceTextureMap::Allocate() {
  FaceTextureElement* result = nullptr;

  if (free) {
    result = free;
    free = free->next;
  } else {
    result = memory_arena_push_type(arena, FaceTextureElement);
  }

  result->next = nullptr;
  return result;
}

void FaceTextureMap::Insert(const char* name, size_t namelen, const char* value, size_t valuelen) {
  u32 bucket = Hash(name, namelen) & (kTextureMapBuckets - 1);

  FaceTextureElement* element = elements[bucket];
  while (element) {
    if (strcmp(element->name, name) == 0) {
      break;
    }
    element = element->next;
  }

  if (element == nullptr) {
    element = Allocate();

    assert(namelen < polymer_array_count(element->name));
    assert(valuelen < polymer_array_count(element->value));

    strncpy(element->name, name, namelen);
    strncpy(element->value, value, valuelen);

    element->name[namelen] = 0;
    element->value[valuelen] = 0;

    element->next = elements[bucket];
    elements[bucket] = element;
  }
}

const char* FaceTextureMap::Find(const char* name, size_t namelen) {
  u32 bucket = Hash(name, namelen) & (kTextureMapBuckets - 1);
  FaceTextureElement* element = elements[bucket];

  while (element) {
    if (strncmp(element->name, name, namelen) == 0) {
      return element->value;
    }
    element = element->next;
  }

  return nullptr;
}

void ParsedBlockModel::InsertTextureMap(FaceTextureMap* map) {
  json_object_element_s* root_element = root->start;

  while (root_element) {
    if (strcmp(root_element->name->string, "textures") == 0) {
      json_object_element_s* texture_element = json_value_as_object(root_element->value)->start;

      while (texture_element) {
        json_string_s* value_string = json_value_as_string(texture_element->value);

        map->Insert(texture_element->name->string, texture_element->name->string_size, value_string->string,
                    value_string->string_size);

        texture_element = texture_element->next;
      }
      break;
    }

    root_element = root_element->next;
  }
}

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
          } else if (strcmp(property_name, "faces") == 0) {
            json_object_element_s* face_obj_element = json_value_as_object(element_property->value)->start;
            while (face_obj_element) {
              const char* facename = face_obj_element->name->string;

              size_t face_index = 0;

              if (strcmp(facename, "down") == 0) {
                face_index = 0;
              } else if (strcmp(facename, "up") == 0) {
                face_index = 1;
              } else if (strcmp(facename, "north") == 0) {
                face_index = 2;
              } else if (strcmp(facename, "south") == 0) {
                face_index = 3;
              } else if (strcmp(facename, "west") == 0) {
                face_index = 4;
              } else if (strcmp(facename, "east") == 0) {
                face_index = 5;
              }

              json_object_element_s* face_element = json_value_as_object(face_obj_element->value)->start;
              RenderableFace* face = model->elements[model->element_count].faces + face_index;

              face->uv_from = Vector2f(0, 0);
              face->uv_to = Vector2f(1, 1);
              face->render = true;
              face->tintindex = 0xFFFF;

              while (face_element) {
                const char* face_property = face_element->name->string;

                if (strcmp(face_property, "texture") == 0) {
                  json_string_s* texture_str = json_value_as_string(face_element->value);
                  const char* texture_name = texture_str->string;

                  size_t namelen = texture_str->string_size;

                  while (texture_name[0] == '#') {
                    texture_name = texture_face_map->Find(texture_name + 1, namelen - 1);

                    if (texture_name == nullptr) {
                      return;
                    }

                    namelen = strlen(texture_name);
                  }

                  size_t prefix_size = 6;

                  if (strstr(texture_name, ":")) {
                    prefix_size = 16;
                  }

                  char lookup[1024];
                  sprintf(lookup, "%.*s.png", (u32)(namelen - prefix_size), texture_name + prefix_size);

                  u32* texture_id = texture_id_map->Find(lookup);

                  if (texture_id) {
                    face->texture_id = *texture_id;
                  } else {
                    face->texture_id = 0;
                  }
                } else if (strcmp(face_property, "uv") == 0) {
                  Vector2f uv_from;
                  Vector2f uv_to;

                  json_array_element_s* value = json_value_as_array(face_element->value)->start;

                  uv_from[0] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;
                  value = value->next;
                  uv_from[1] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;
                  value = value->next;
                  uv_to[0] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;
                  value = value->next;
                  uv_to[1] = strtol(json_value_as_number(value->value)->number, nullptr, 10) / 16.0f;

                  face->uv_from = uv_from;
                  face->uv_to = uv_to;
                } else if (strcmp(face_property, "tintindex") == 0) {
                  face->tintindex = (u32)strtol(json_value_as_number(face_element->value)->number, nullptr, 10);
                  // if (strstr(path, "leaves") != 0) {
                  // face->tintindex = 1;
                  //}
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

AssetLoader::~AssetLoader() {
  Cleanup();
}

bool AssetLoader::OpenArchive(const char* filename) {
  snapshot = arena->GetSnapshot();

  return archive.Open(filename);
}

void AssetLoader::CloseArchive() {
  archive.Close();
  arena->Revert(snapshot);
}

bool AssetLoader::Load(const char* jar_path, const char* blocks_path) {
  if (!OpenArchive(jar_path)) {
    return false;
  }

  if (ParseBlockModels() == 0) {
    CloseArchive();
    return false;
  }

  if (ParseBlockStates() == 0) {
    CloseArchive();
    return false;
  }

  if (LoadTextures() == 0) {
    CloseArchive();
    return false;
  }

  if (!ParseBlocks(blocks_path)) {
    CloseArchive();
    return false;
  }

  for (size_t i = 0; i < state_count; ++i) {
    json_object_element_s* root_element = states[i].root->start;
    char* blockstate_filename = states[i].filename;
    blockstate_filename[strlen(blockstate_filename) - 5] = 0;

    while (root_element) {
      if (strncmp("variants", root_element->name->string, root_element->name->string_size) == 0) {
        json_object_s* variant_obj = json_value_as_object(root_element->value);

        for (size_t bid = 0; bid < final_state_count; ++bid) {
          char* name = final_states[bid].info->name + 10;

          if (final_states[bid].model.element_count > 0) {
            continue;
          }

          if (strcmp(name, blockstate_filename) != 0) {
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
                  json_string_s* model_name_str = json_value_as_string(state_element->value);

                  // Do a lookup on the model name then store the model in the BlockState.
                  // Model lookup is going to need to be recursive with the root parent data being filled out first then
                  // cascaded down.
                  const size_t kPrefixSize = 16;

                  ArenaSnapshot snapshot = arena->GetSnapshot();
                  FaceTextureMap texture_face_map(arena);

                  final_states[bid].model =
                      LoadModel(model_name_str->string + kPrefixSize, model_name_str->string_size - kPrefixSize,
                                &texture_face_map, &texture_id_map);
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

    blockstate_filename[strlen(blockstate_filename) - 5] = '.';
  }

  CloseArchive();
  return true;
}

BlockModel AssetLoader::LoadModel(const char* path, size_t path_size, FaceTextureMap* texture_face_map,
                                  TextureIdMap* texture_id_map) {
  BlockModel result = {};

  ParsedBlockModel* parsed_model = nullptr;

  const size_t kPrefixSkip = 30;

  for (size_t i = 0; i < model_count && parsed_model == nullptr; ++i) {
    char* check = models[i].filename + kPrefixSkip;
    char* separator = strstr(check, ".");

    if (separator == nullptr) {
      continue;
    }

    *separator = 0;

    if (strcmp(check, path) == 0) {
      parsed_model = models + i;
    }

    *separator = '.';
  }

  if (parsed_model == nullptr) {
    return result;
  }

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

      BlockModel parent =
          LoadModel(parent_name->string + prefix_size, parent_name->string_size, texture_face_map, texture_id_map);
      for (size_t i = 0; i < parent.element_count; ++i) {
        result.elements[result.element_count++] = parent.elements[i];

        assert(result.element_count < polymer_array_count(result.elements));
      }
    }

    root_element = root_element->next;
  }

  for (size_t i = 0; i < result.element_count; ++i) {
    BlockElement* element = result.elements + i;

    element->occluding = element->from == Vector3f(0, 0, 0) && element->to == Vector3f(1, 1, 1);
  }

  return result;
}

bool AssetLoader::ParseBlocks(const char* blocks_filename) {
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

  final_state_count = (size_t)GetLastStateId(root_obj) + 1;
  assert(final_state_count > 1);

  // Create a list of pointers to property strings stored in the transient arena
  properties = (char**)arena->Allocate(sizeof(char*) * final_state_count);
  final_states = memory_arena_push_type_count(perm_arena, BlockState, final_state_count);
  block_infos = (BlockStateInfo*)memory_arena_push_type_count(perm_arena, BlockStateInfo, root_obj->length);

  json_object_element_s* element = root_obj->start;

  size_t block_state_index = 0;

  while (element) {
    json_object_s* block_obj = json_value_as_object(element->value);
    assert(block_obj);

    BlockStateInfo* info = block_infos + block_info_count++;
    assert(element->name->string_size < polymer_array_count(info->name));
    memcpy(info->name, element->name->string, element->name->string_size);

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
              final_states[block_state_index].info = info;

              long block_id = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              final_states[block_state_index].id = block_id;

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

  free(root);

  return true;
}

u32 AssetLoader::GetLastStateId(json_object_s* root) {
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

size_t AssetLoader::LoadTextures() {
  ZipArchiveElement* texture_files = archive.ListFiles(arena, "assets/minecraft/textures/block/", &texture_count);

  if (texture_count == 0) {
    return 0;
  }

  this->texture_images = memory_arena_push_type_count(arena, u8, kTextureSize * texture_count);

  for (u32 i = 0; i < texture_count; ++i) {
    size_t size = 0;
    u8* raw_image = (u8*)archive.ReadFile(arena, texture_files[i].name, &size);
    int width, height, channels;

    // TODO: Can it be loaded directly into memory?
    stbi_uc* image = stbi_load_from_memory(raw_image, (int)size, &width, &height, &channels, STBI_rgb_alpha);
    if (image == nullptr) {
      continue;
    }

    char* texture_name = texture_files[i].name + 32;

    this->texture_id_map.Insert(texture_name, i);

    u8* destination = texture_images + i * kTextureSize;

    memcpy(destination, image, kTextureSize);

    stbi_image_free(image);
  }

  return texture_count;
}

size_t AssetLoader::ParseBlockStates() {
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

    strcpy(states[i].filename, state_files[i].name + kBlockStateAssetSkip);
  }

  return state_count;
}

size_t AssetLoader::ParseBlockModels() {
  ZipArchiveElement* files = archive.ListFiles(arena, "assets/minecraft/models/block", &model_count);

  models = memory_arena_push_type_count(arena, ParsedBlockModel, model_count);

  for (size_t i = 0; i < model_count; ++i) {
    size_t size = 0;
    char* data = archive.ReadFile(arena, files[i].name, &size);

    assert(data);

    strcpy(models[i].filename, files[i].name);

    models[i].root_value = json_parse(data, size);
    assert(models[i].root_value->type == json_type_object);

    models[i].root = json_value_as_object(models[i].root_value);
  }

  return model_count;
}

u8* AssetLoader::GetTexture(size_t index) {
  assert(index < texture_count);
  return texture_images + index * kTextureSize;
}

void AssetLoader::Cleanup() {
  for (size_t i = 0; i < model_count; ++i) {
    free(models[i].root_value);
  }
  model_count = 0;

  CloseArchive();
}

} // namespace polymer
