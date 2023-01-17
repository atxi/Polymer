#include <polymer/asset/block_assets.h>

#include <polymer/asset/asset_system.h>
#include <polymer/asset/block_model_rotate.h>
#include <polymer/asset/parsed_block_model.h>
#include <polymer/bitset.h>
#include <polymer/math.h>
#include <polymer/memory.h>
#include <polymer/render/chunk_renderer.h>
#include <polymer/render/render.h>
#include <polymer/world/block.h>

#include <polymer/json.h>
#include <polymer/zip_archive.h>

#include <polymer/stb_image.h>

using polymer::render::RenderLayer;
using polymer::world::BlockElement;
using polymer::world::BlockFace;
using polymer::world::BlockModel;
using polymer::world::BlockRegistry;
using polymer::world::BlockState;
using polymer::world::BlockStateInfo;
using polymer::world::RenderableFace;

namespace polymer {
namespace asset {

constexpr size_t kTextureSize = 16 * 16 * 4;
constexpr size_t kNamespaceSize = 10; // "minecraft:"
                                      // Amount of characters to skip over to get to the blockmodel asset name
constexpr size_t kBlockModelAssetSkip = 24;

typedef HashMap<MapStringKey, ParsedBlockModel*, MapStringHasher> ParsedBlockMap;

struct ParsedBlockState {
  String filename;

  json_value_s* root_value;
  json_object_s* root;
};

struct AssetParser {
  MemoryArena* arena;
  BlockRegistry* registry;

  ZipArchive& archive;

  TextureIdMap texture_id_map;
  TextureIdMap* full_texture_id_map = nullptr;
  ParsedBlockMap parsed_block_map;

  size_t model_count;
  ParsedBlockModel* models = nullptr;

  size_t state_count;
  ParsedBlockState* states = nullptr;

  BitSet default_state_set;

  size_t texture_count;
  u8* texture_images;
  render::TextureConfig* texture_configs;

  AssetParser(MemoryArena* arena, BlockRegistry* registry, ZipArchive& archive)
      : arena(arena), registry(registry), archive(archive), model_count(0), parsed_block_map(*arena),
        texture_id_map(*arena) {}

  void ParseModel(const char* filename, ParsedBlockModel& model);
  size_t ParseBlockModels();
  size_t ParseBlockStates();
  bool ParseBlocks(MemoryArena* perm_arena, const char* blocks_filename);

  size_t LoadTextures();

  void ResolveModel(ParsedBlockModel& model);
  void ResolveModels(MemoryArena& perm_arena);
  void HandleMultipartApplyNode(MemoryArena& perm_arena, size_t bid, json_object_s* apply_obj, BitSet& element_set);
  bool ResolveMultiparts(MemoryArena& perm_arena, BitSet& element_set, ParsedBlockState* parsed_state,
                         const String& blockstate_name);
  bool ResolveVariants(MemoryArena& perm_arena, BitSet& element_set, ParsedBlockState* parsed_state,
                       const String& blockstate_name);

  bool IsTransparentTexture(u32 texture_id);
  void AssignModelRenderSettings(ParsedBlockModel& parsed_model);

  inline u8* GetTexture(size_t index) {
    assert(index < texture_count);
    return texture_images + index * kTextureSize;
  }
};

void AssetParser::AssignModelRenderSettings(ParsedBlockModel& parsed_model) {
  BlockModel& model = parsed_model.model;

  // TODO: All of these checks should be pulled into a system for managing texture-specific data.
  String path(parsed_model.filename + kBlockModelAssetSkip);
  bool is_prismarine = poly_strstr(path, "prismarine").data != nullptr;

  bool is_leaves = poly_strstr(path, "leaves").data != nullptr;
  bool is_spruce = false;
  bool is_birch = false;

  // TODO: This should be removed once biome data is handled correctly.
  if (is_leaves) {
    // Spruce and birch have hardcoded coloring so they go into their own tintindex.
    is_spruce = poly_strstr(path, "spruce").data != nullptr;
    is_birch = poly_strstr(path, "birch").data != nullptr;

    model.has_leaves = 1;
  }

  for (size_t i = 0; i < model.element_count; ++i) {
    BlockElement* element = model.elements + i;

    element->occluding = element->from == Vector3f(0, 0, 0) && element->to == Vector3f(1, 1, 1);

    if (element->occluding) {
      model.has_occluding = 1;
    }

    if (element->shade) {
      model.has_shaded = 1;
    }

    for (size_t j = 0; j < 6; ++j) {
      element->faces[j].transparency = IsTransparentTexture(element->faces[j].texture_id);

      if (element->faces[j].transparency) {
        model.has_transparency = 1;
      }

      if (is_prismarine) {
        // TODO: This should be removed once the meta files are processed
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

      parsed_model.elements[i].faces[j].transparency = element->faces[j].transparency;
      parsed_model.elements[i].faces[j].frame_count = element->faces[j].frame_count;
      parsed_model.elements[i].faces[j].tintindex = element->faces[j].tintindex;
    }
  }

  bool is_glass = poly_contains(path, POLY_STR("/glass.json")) || poly_contains(path, POLY_STR("stained_glass.json"));
  if (is_glass) {
    model.has_glass = true;
  }

  const static String kHorizontalOffsetNames[] = {
      POLY_STR("/mangrove_propagule.json"),
      POLY_STR("/grass.json"),
      POLY_STR("/fern.json"),
      POLY_STR("/dandelion.json"),
      POLY_STR("/poppy.json"),
      POLY_STR("/blue_orchid.json"),
      POLY_STR("/allium.json"),
      POLY_STR("/azure_bluet.json"),
      POLY_STR("_tulip.json"),
      POLY_STR("/oxeye_daisy.json"),
      POLY_STR("/cornflower.json"),
      POLY_STR("/lily_of_the_valley.json"),
      POLY_STR("/bamboo_sapling.json"),
      POLY_STR("/bamboo1_age"),
      POLY_STR("/bamboo2_age"),
      POLY_STR("/bamboo3_age"),
      POLY_STR("/bamboo4_age"),
      POLY_STR("/wither_rose.json"),
      POLY_STR("/crimson_roots.json"),
      POLY_STR("/warped_roots.json"),
      POLY_STR("/nether_sprouts.json"),
      POLY_STR("/tall_grass_"),
      POLY_STR("/large_fern_"),
      POLY_STR("/sunflower_"),
      POLY_STR("/lilac_"),
      POLY_STR("/rose_bush_"),
      POLY_STR("/peony_"),
  };

  for (size_t i = 0; i < polymer_array_count(kHorizontalOffsetNames); ++i) {
    if (poly_contains(parsed_model.filename, kHorizontalOffsetNames[i])) {
      model.random_horizontal_offset = 1;
      break;
    }
  }

  if (poly_contains(parsed_model.filename, POLY_STR("/grass.json"))) {
    model.random_vertical_offset = 1;
  } else if (poly_contains(parsed_model.filename, POLY_STR("/fern.json"))) {
    model.random_vertical_offset = 1;
  }
}

static void AssignFaceRenderSettings(RenderableFace* face, const String& texture) {
  if (poly_contains(texture, POLY_STR("leaves"))) {
    face->render_layer = (int)RenderLayer::Leaves;
  } else if (poly_strcmp(texture, POLY_STR("water_still.png")) == 0) {
    face->render_layer = (int)RenderLayer::Alpha;
  } else if (poly_strcmp(texture, POLY_STR("nether_portal.png")) == 0) {
    face->render_layer = (int)RenderLayer::Alpha;
  } else if (poly_contains(texture, POLY_STR("stained_glass.png"))) {
    face->render_layer = (int)RenderLayer::Alpha;
  } else if (poly_strcmp(texture, POLY_STR("grass.png")) == 0) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_strcmp(texture, POLY_STR("sugar_cane.png")) == 0) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("grass_bottom.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_contains(texture, POLY_STR("grass_top.png"))) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_strcmp(texture, POLY_STR("fern.png")) == 0) {
    face->render_layer = (int)RenderLayer::Flora;
  } else if (poly_strcmp(texture, POLY_STR("grass_block_top.png")) == 0) {
    face->random_flip = 1;
  } else if (poly_strcmp(texture, POLY_STR("stone.png")) == 0) {
    face->random_flip = 1;
  } else if (poly_strcmp(texture, POLY_STR("sand.png")) == 0) {
    face->random_flip = 1;
  }
}

static inline render::TextureConfig CreateTextureConfig(String texture_name) {
  render::TextureConfig cfg(true);

  if (poly_contains(texture_name, POLY_STR("leaves"))) {
    cfg.brighten_mipping = false;
  }

  return cfg;
}

bool BlockAssetLoader::Load(render::VulkanRenderer& renderer, ZipArchive& archive, const char* blocks_path,
                            world::BlockRegistry* registry) {
  assets = memory_arena_push_type(&perm_arena, BlockAssets);

  assets->block_registry = registry;
  assets->block_registry->info_count = 0;
  assets->block_registry->state_count = 0;
  assets->block_registry->name_map.Clear();

  assets->texture_id_map = memory_arena_construct_type(&perm_arena, TextureIdMap, perm_arena);

  AssetParser parser(&trans_arena, assets->block_registry, archive);

  parser.full_texture_id_map = assets->texture_id_map;

  if (!parser.ParseBlockModels()) {
    return false;
  }

  if (!parser.ParseBlockStates()) {
    return false;
  }

  if (parser.LoadTextures() == 0) {
    return false;
  }

  if (!parser.ParseBlocks(&perm_arena, blocks_path)) {
    return false;
  }

  parser.ResolveModels(perm_arena);

  size_t texture_count = parser.texture_count;

  assets->block_textures = renderer.CreateTextureArray(16, 16, texture_count);

  if (!assets->block_textures) {
    return false;
  }

  render::TextureArrayPushState push_state = renderer.BeginTexturePush(*assets->block_textures);

  for (size_t i = 0; i < texture_count; ++i) {
    const render::TextureConfig& cfg = parser.texture_configs[i];

    renderer.PushArrayTexture(trans_arena, push_state, parser.GetTexture(i), i, cfg);
  }

  renderer.CommitTexturePush(push_state);

  for (size_t i = 0; i < assets->block_registry->state_count; ++i) {
    world::BlockState* state = assets->block_registry->states + i;
    world::BlockStateInfo* info = state->info;

    String key(info->name, info->name_length);
    world::BlockIdRange* range = assets->block_registry->name_map.Find(key);

    if (range == nullptr) {
      world::BlockIdRange mapping(state->id, 1);

      assets->block_registry->name_map.Insert(key, mapping);
    } else {
      ++range->count;
    }
  }

  return true;
}

// Recursively parse the model by parsing parents first.
void AssetParser::ParseModel(const char* filename, ParsedBlockModel& model) {
  size_t size = 0;
  char* data = archive.ReadFile(arena, filename, &size);

  assert(data);

  json_value_s* root_value = json_parse(data, size);

  assert(root_value->type == json_type_object);

  json_object_s* root = json_value_as_object(root_value);

  String parent_name = model.GetParentName(root);

  if (parent_name.size > 0) {
    size_t prefix_size = poly_contains(parent_name, ':') ? kNamespaceSize : 0;
    parent_name.data += prefix_size;
    parent_name.size -= prefix_size;

    ParsedBlockModel** parent_model = parsed_block_map.Find(MapStringKey(parent_name.data, parent_name.size));

    if (parent_model == nullptr) {
      fprintf(stderr, "Failed to find parent model for %s\n", filename);
      return;
    }

    if (!(*parent_model)->parsed) {
      char parent_filename[256];

      sprintf(parent_filename, "assets/minecraft/models/%.*s.json", (u32)parent_name.size, parent_name.data);
      ParseModel(parent_filename, **parent_model);
    }

    model.parent = *parent_model;
  }

  if (!model.Parse(*arena, filename, root)) {
    fprintf(stderr, "Failed to parse BlockModel %s\n", filename);
  }

  free(root_value);
}

size_t AssetParser::ParseBlockModels() {
  ZipArchiveElement* files = archive.ListFiles(arena, "assets/minecraft/models/block", &model_count);

  if (model_count == 0) {
    return 0;
  }

  models = memory_arena_push_type_count(arena, ParsedBlockModel, model_count);

  memset(models, 0, sizeof(ParsedBlockModel) * model_count);

  for (size_t i = 0; i < model_count; ++i) {
    String filename(files[i].name + kBlockModelAssetSkip, strlen(files[i].name) - kBlockModelAssetSkip - 5);

    parsed_block_map.Insert(MapStringKey(filename.data, filename.size), models + i);
  }

  for (size_t i = 0; i < model_count; ++i) {
    if (models[i].parsed) continue;

    ParseModel(files[i].name, models[i]);
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
  this->texture_configs = memory_arena_push_type_count(arena, render::TextureConfig, state_count * 4);

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

      render::TextureConfig cfg = CreateTextureConfig(texture_name);

      for (u32 j = 0; j < range.count; ++j) {
        texture_configs[current_texture_id] = cfg;
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

static u32 GetHighestStateId(json_object_s* root) {
  u32 highest_id = 0;

  json_object_element_s* root_child = root->start;

  while (root_child) {
    json_object_s* type_element = json_value_as_object(root_child->value);
    json_object_element_s* type_element_child = type_element->start;

    while (type_element_child) {
      if (strncmp(type_element_child->name->string, "states", type_element_child->name->string_size) == 0) {
        json_array_s* states = json_value_as_array(type_element_child->value);
        json_array_element_s* state_array_child = states->start;

        while (state_array_child) {
          json_object_s* state_obj = json_value_as_object(state_array_child->value);
          json_object_element_s* state_child = state_obj->start;

          while (state_child) {
            if (strncmp(state_child->name->string, "id", state_child->name->string_size) == 0) {
              u32 id = (u32)strtol(json_value_as_number(state_child->value)->number, nullptr, 10);

              if (id > highest_id) {
                highest_id = id;
              }
              break;
            }

            state_child = state_child->next;
          }

          state_array_child = state_array_child->next;
        }

        break;
      }

      type_element_child = type_element_child->next;
    }

    root_child = root_child->next;
  }

  return highest_id;
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

  registry->state_count = (size_t)GetHighestStateId(root_obj) + 1;
  assert(registry->state_count > 1);

  default_state_set = BitSet(*this->arena, registry->state_count);

  // Create a list of pointers to property strings stored in the transient arena
  registry->states = memory_arena_push_type_count(perm_arena, BlockState, registry->state_count);
  registry->properties = memory_arena_push_type_count(perm_arena, String, registry->state_count);
  registry->infos = (BlockStateInfo*)memory_arena_push_type_count(perm_arena, BlockStateInfo, root_obj->length);

  json_object_element_s* element = root_obj->start;

  while (element) {
    json_object_s* block_obj = json_value_as_object(element->value);
    assert(block_obj);

    BlockStateInfo* info = registry->infos + registry->info_count++;
    assert(element->name->string_size < polymer_array_count(info->name));
    memcpy(info->name, element->name->string, element->name->string_size);
    info->name_length = element->name->string_size;
    info->name[info->name_length] = 0;

    json_object_element_s* block_element = block_obj->start;
    while (block_element) {
      if (strncmp(block_element->name->string, "states", block_element->name->string_size) == 0) {
        json_array_s* states = json_value_as_array(block_element->value);
        json_array_element_s* state_array_element = states->start;

        while (state_array_element) {
          json_object_s* state_obj = json_value_as_object(state_array_element->value);

          json_object_element_s* state_element = state_obj->start;

          u32 id = 0;

          while (state_element) {
            if (strncmp(state_element->name->string, "id", state_element->name->string_size) == 0) {
              long block_id = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              registry->states[block_id].info = info;
              registry->states[block_id].id = block_id;
              registry->properties[block_id].data = 0;
              registry->properties[block_id].size = 0;

              id = (u32)block_id;
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
              char* property = (char*)perm_arena->Allocate(0, 4);
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

                char* p = (char*)perm_arena->Allocate(alloc_size, 1);

                // Allocate space for a comma to separate the properties
                if (property_element != property_object->start) {
                  perm_arena->Allocate(1, 1);
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

              registry->properties[id].data = property;
              registry->properties[id].size = property_length;
            } else if (strncmp(state_element->name->string, "default", state_element->name->string_size) == 0) {
              default_state_set.Set(id, 1);
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

void AssetParser::ResolveModel(ParsedBlockModel& parsed_model) {
  // Convert the intermediate parsed storage into final BlockModel representation, and resolve all textures.
  BlockModel& model = parsed_model.model;

  model.element_count = parsed_model.element_count;
  model.ambient_occlusion = parsed_model.ambient_occlusion;

  for (size_t i = 0; i < parsed_model.element_count; ++i) {
    BlockElement* element = model.elements + i;
    model.elements[i].from = parsed_model.elements[i].from;
    model.elements[i].to = parsed_model.elements[i].to;

    model.elements[i].occluding = parsed_model.elements[i].occluding;
    model.elements[i].shade = parsed_model.elements[i].shade;
    model.elements[i].rescale = parsed_model.elements[i].rotation.rescale;
    model.elements[i].occluding = 1;

    for (size_t j = 0; j < 6; ++j) {
      ParsedRenderableFace* parsed_face = parsed_model.elements[i].faces + j;
      RenderableFace* model_face = model.elements[i].faces + j;

      model_face->uv_from = parsed_face->uv_from;
      model_face->uv_to = parsed_face->uv_to;

      model_face->render = parsed_face->render;
      model_face->transparency = parsed_face->transparency;
      model_face->cullface = parsed_face->cullface;
      model_face->render_layer = parsed_face->render_layer;
      model_face->random_flip = parsed_face->random_flip;
      model_face->tintindex = parsed_face->tintindex;
      model_face->texture_id = 0;
      model_face->frame_count = 0;

      // Fill out the face uv info based on the actual element instead of the entire uv in the case of non-existence.
      if (!parsed_face->custom_uv) {
        BlockFace side = (BlockFace)j;

        switch (side) {
        case BlockFace::Down: {
          model_face->uv_from = Vector2f(element->from.x, element->from.z);
          model_face->uv_to = Vector2f(element->to.x, element->to.z);
        } break;
        case BlockFace::Up: {
          model_face->uv_from = Vector2f(element->from.x, element->from.z);
          model_face->uv_to = Vector2f(element->to.x, element->to.z);
        } break;
        case BlockFace::North: {
          model_face->uv_to = Vector2f(element->to.x, element->to.y);
          model_face->uv_from = Vector2f(element->from.x, element->from.y);
        } break;
        case BlockFace::South: {
          model_face->uv_from = Vector2f(element->from.x, element->from.y);
          model_face->uv_to = Vector2f(element->to.x, element->to.y);
        } break;
        case BlockFace::West: {
          model_face->uv_from = Vector2f(element->from.z, element->from.y);
          model_face->uv_to = Vector2f(element->to.z, element->to.y);
        } break;
        case BlockFace::East: {
          model_face->uv_from = Vector2f(element->from.z, element->from.y);
          model_face->uv_to = Vector2f(element->to.z, element->to.y);
        } break;
        }
      }

      if (parsed_face->texture_name_size <= 0) continue;

      String texture_name =
          parsed_model.ResolveTexture(String(parsed_face->texture_name, parsed_face->texture_name_size));

      // Texture wasn't fully resolved
      if (texture_name.size <= 0 || texture_name.data[0] == '#') continue;

      size_t prefix_size = poly_contains(texture_name, ':') ? 16 : 6;

      char lookup[1024];
      size_t lookup_size =
          sprintf(lookup, "%.*s.png", (u32)(texture_name.size - prefix_size), texture_name.data + prefix_size);
      String texture_search(lookup, lookup_size);

      TextureIdRange* texture_range = texture_id_map.Find(MapStringKey(texture_search.data, texture_search.size));

      if (!texture_range) {
        fprintf(stderr, "Failed to find texture %.*s\n", (u32)texture_name.size, texture_name.data);
        continue;
      }

      model_face->texture_id = texture_range->base;
      model_face->frame_count = texture_range->count;
      parsed_face->texture_id = texture_range->base;
      parsed_face->frame_count = texture_range->count;

      AssignFaceRenderSettings(model_face, texture_search);

      parsed_face->render_layer = model_face->render_layer;
      parsed_face->random_flip = model_face->random_flip;
    }
  }

  AssignModelRenderSettings(parsed_model);
}

inline json_object_element_s* GetJsonObjectElement(json_object_s* obj, const String& name) {
  json_object_element_s* element = obj->start;

  while (element) {
    String element_name(element->name->string, element->name->string_size);

    if (poly_strcmp(element_name, name) == 0) {
      return element;
    }

    element = element->next;
  }

  return nullptr;
}

static bool HasPropertyValue(const String& properties, const String& name, const String& value) {
  if (name.size == 0 || properties.size == 0) return true;

  String current_name(properties.data, 0);
  String current_value(properties.data, 0);
  bool parsing_value = false;
  for (size_t i = 0; i < properties.size; ++i) {
    char c = properties.data[i];

    if (!parsing_value) {
      if (c == '=') {
        current_value.data = properties.data + i + 1;
        current_value.size = 0;
        parsing_value = true;
      } else {
        ++current_name.size;
      }
    } else {
      if (c == ',') {
        if (poly_strcmp(current_name, name) == 0) {
          return poly_strcmp(current_value, value) == 0;
        }

        current_name.data = properties.data + i + 1;
        current_name.size = 0;
        parsing_value = false;
      } else {
        ++current_value.size;
      }
    }
  }

  // It should never end while parsing a property name
  assert(parsing_value);

  if (poly_strcmp(current_name, name) == 0) {
    return poly_strcmp(current_value, value) == 0;
  }

  return false;
}

static bool HasPropertyValue(BlockRegistry* registry, size_t bid, const String& name, const String& value) {
  String* properties = registry->properties + bid;

  if (name.size == 0 && properties == nullptr || properties->size == 0) return true;
  if (!properties || properties->size == 0) return false;

  return HasPropertyValue(*properties, name, value);
}

static inline bool HasProperties(BlockRegistry* registry, size_t bid, json_array_s* arr, bool require_all) {
  json_array_element_s* arr_ele = arr->start;

  while (arr_ele) {
    json_object_s* obj = json_value_as_object(arr_ele->value);

    arr_ele = arr_ele->next;

    json_object_element_s* ele = obj->start;

    while (ele) {
      assert(ele->value->type == json_type_string);

      String property_name(ele->name->string, ele->name->string_size);

      json_string_s* property_value_str = json_value_as_string(ele->value);
      String property_value(property_value_str->string, property_value_str->string_size);

      if (HasPropertyValue(registry, bid, property_name, property_value)) {
        if (!require_all) {
          return true;
        }
      } else {
        if (require_all) {
          return false;
        }
      }

      ele = ele->next;
    }
  }

  return require_all;
}

static bool HasPropertySet(const String& check_set, const String& required_set) {
  if (required_set.size == 0) return true;
  if (check_set.size == 0) return false;

  String current_name(required_set.data, 0);
  String current_value(required_set.data, 0);
  bool parsing_value = false;

  for (size_t i = 0; i < required_set.size; ++i) {
    char c = required_set.data[i];

    if (!parsing_value) {
      if (c == '=') {
        current_value.data = required_set.data + i + 1;
        current_value.size = 0;
        parsing_value = true;
      } else {
        ++current_name.size;
      }
    } else {
      if (c == ',') {
        // The name and value were just parsed, so check the 'check_set' to see if it has it
        if (!HasPropertyValue(check_set, current_name, current_value)) {
          return false;
        }

        current_name.data = required_set.data + i + 1;
        current_name.size = 0;
        parsing_value = false;
      } else {
        ++current_value.size;
      }
    }
  }

  // It should never end while parsing a property name
  assert(parsing_value);

  return HasPropertyValue(check_set, current_name, current_value);
}

inline void ApplyMultipartModel(BlockModel& target_model, BlockModel& model) {
  if (target_model.element_count == 0) {
    memcpy(&target_model, &model, sizeof(BlockModel));
    return;
  }

  if (!model.ambient_occlusion) {
    target_model.ambient_occlusion = false;
  }

  for (size_t i = 0; i < model.element_count; ++i) {
    BlockElement* element = target_model.elements + target_model.element_count++;

    assert(target_model.element_count <= polymer_array_count(model.elements));

    memcpy(element, model.elements + i, sizeof(BlockElement));
  }
}

void AssetParser::HandleMultipartApplyNode(MemoryArena& perm_arena, size_t bid, json_object_s* apply_obj,
                                           BitSet& element_set) {
  json_object_element_s* model_element = GetJsonObjectElement(apply_obj, POLY_STR("model"));

  if (!model_element) {
    fprintf(stderr, "Invalid multipart. Apply element did not have a model to apply.\n");
    return;
  }

  json_string_s* model_str = json_value_as_string(model_element->value);
  String model_name(model_str->string, model_str->string_size);

  size_t prefix_size = poly_contains(model_name, ':') ? 10 : 0;
  model_name.data += prefix_size;
  model_name.size -= prefix_size;

  json_object_element_s* x_element = GetJsonObjectElement(apply_obj, POLY_STR("x"));
  json_object_element_s* y_element = GetJsonObjectElement(apply_obj, POLY_STR("y"));
  json_object_element_s* uvlock_element = GetJsonObjectElement(apply_obj, POLY_STR("uvlock"));

  Vector3i rotation;

  if (x_element) {
    rotation.x = strtol(json_value_as_number(x_element->value)->number, nullptr, 10);
  }

  if (y_element) {
    rotation.y = strtol(json_value_as_number(y_element->value)->number, nullptr, 10);
  }

  ParsedBlockModel** parsed_model = parsed_block_map.Find(MapStringKey(model_name.data, model_name.size));

  if (parsed_model && (*parsed_model)->parsed) {
    size_t variant_start = registry->states[bid].model.element_count;
    ApplyMultipartModel(registry->states[bid].model, (*parsed_model)->model);
    size_t variant_count = registry->states[bid].model.element_count - variant_start;
    RotateVariant(perm_arena, registry->states[bid].model, **parsed_model, variant_start, variant_count, rotation,
                  uvlock_element != nullptr);

    element_set.Set(bid, 1);
  } else {
    printf("Failed to find parsed_model %.*s\n", (u32)model_name.size, model_name.data);
  }
}

bool AssetParser::ResolveMultiparts(MemoryArena& perm_arena, BitSet& element_set, ParsedBlockState* parsed_state,
                                    const String& blockstate_name) {
  json_object_element_s* multipart_obj = GetJsonObjectElement(parsed_state->root, POLY_STR("multipart"));
  if (!multipart_obj) {
    return false;
  }

  json_array_s* multipart_array = json_value_as_array(multipart_obj->value);

  // Go through each blockstate to perform resolution of the parsed state.
  for (size_t bid = 0; bid < registry->state_count; ++bid) {
    // If this block is in the element_set then it was already assigned and must not be this one.
    if (element_set.IsSet(bid)) continue;

    String state_name(registry->states[bid].info->name + kNamespaceSize,
                      registry->states[bid].info->name_length - kNamespaceSize);

    if (poly_strcmp(state_name, blockstate_name) != 0) {
      continue;
    }

    String* properties = registry->properties + bid;

    json_array_element_s* multipart_element = multipart_array->start;

    while (multipart_element) {
      json_object_s* definition_obj = json_value_as_object(multipart_element->value);
      multipart_element = multipart_element->next;

      json_object_element_s* apply_element = GetJsonObjectElement(definition_obj, POLY_STR("apply"));
      json_object_element_s* when_element = GetJsonObjectElement(definition_obj, POLY_STR("when"));

      if (!apply_element) {
        fprintf(stderr, "Invalid multipart. Did not contain apply element.\n");
        continue;
      }

      if (!when_element) {
        // Apply any unconditional multi-part models
        if (apply_element->value->type == json_type_object) {
          json_object_s* apply_obj = json_value_as_object(apply_element->value);

          HandleMultipartApplyNode(perm_arena, bid, apply_obj, element_set);
        } else if (apply_element->value->type == json_type_array) {
          json_array_s* apply_array = json_value_as_array(apply_element->value);

          json_array_element_s* apply_array_ele = apply_array->start;
          while (apply_array_ele) {
            json_object_s* apply_obj = json_value_as_object(apply_array_ele->value);

            HandleMultipartApplyNode(perm_arena, bid, apply_obj, element_set);

            apply_array_ele = apply_array_ele->next;
          }
        }
      } else {
        json_object_s* when_obj = json_value_as_object(when_element->value);
        json_object_element_s* when_obj_element = when_obj->start;

        bool matches = true;

        // All of the when element names must be in the property string with the correct values
        while (when_obj_element) {
          if (when_obj_element->value->type == json_type_array) {
            String when_type(when_obj_element->name->string, when_obj_element->name->string_size);

            json_array_s* property_array = json_value_as_array(when_obj_element->value);

            if (poly_strcmp(when_type, POLY_STR("AND")) == 0) {
              matches = HasProperties(registry, bid, property_array, true);
            } else if (poly_strcmp(when_type, POLY_STR("OR")) == 0) {
              matches = HasProperties(registry, bid, property_array, false);
            } else {
              assert(!"When combine type not handled");
            }
          } else if (when_obj_element->value->type == json_type_string) {
            String when_property_name(when_obj_element->name->string, when_obj_element->name->string_size);
            json_string_s* when_property_value_str = json_value_as_string(when_obj_element->value);
            String when_property_value(when_property_value_str->string, when_property_value_str->string_size);

            if (!HasPropertyValue(registry, bid, when_property_name, when_property_value)) {
              matches = false;
              break;
            }
          }

          when_obj_element = when_obj_element->next;
        }

        if (matches) {
          if (apply_element->value->type == json_type_object) {
            json_object_s* apply_obj = json_value_as_object(apply_element->value);

            HandleMultipartApplyNode(perm_arena, bid, apply_obj, element_set);
          } else if (apply_element->value->type == json_type_array) {
            json_array_s* apply_array = json_value_as_array(apply_element->value);

            json_array_element_s* apply_array_ele = apply_array->start;
            while (apply_array_ele) {
              json_object_s* apply_obj = json_value_as_object(apply_array_ele->value);

              HandleMultipartApplyNode(perm_arena, bid, apply_obj, element_set);

              apply_array_ele = apply_array_ele->next;
            }
          }
        }
      }
    }
  }

  return true;
}

bool AssetParser::ResolveVariants(MemoryArena& perm_arena, BitSet& element_set, ParsedBlockState* parsed_state,
                                  const String& blockstate_name) {
  json_object_element_s* root_element = parsed_state->root->start;

  json_object_s* variant_obj = nullptr;

  // Find the variant object that must be an element of the root blockstate json.
  while (root_element) {
    String element_name(root_element->name->string, root_element->name->string_size);

    if (poly_strcmp(element_name, POLY_STR("variants")) == 0) {
      variant_obj = json_value_as_object(root_element->value);
    }

    root_element = root_element->next;
  }

  if (!variant_obj) return false;

  // Go through each blockstate to perform resolution of the parsed state.
  for (size_t bid = 0; bid < registry->state_count; ++bid) {
    // If this block is in the element_set then it was already assigned and must not be this one.
    if (element_set.IsSet(bid)) continue;

    String state_name(registry->states[bid].info->name + kNamespaceSize,
                      registry->states[bid].info->name_length - kNamespaceSize);

    if (poly_strcmp(state_name, blockstate_name) != 0) {
      continue;
    }

    json_object_element_s* variant_element = variant_obj->start;

    while (variant_element) {
      String variant_string(variant_element->name->string, variant_element->name->string_size);

      String* properties = &registry->properties[bid];

      if ((properties && HasPropertySet(*properties, variant_string)) || (variant_string.size == 0 && !properties)) {
        json_object_s* state_details = nullptr;

        if (variant_element->value->type == json_type_array) {
          // TODO: Find out why multiple models are listed under one variant type. Just default to first for now.
          state_details = json_value_as_object(json_value_as_array(variant_element->value)->start->value);
        } else {
          state_details = json_value_as_object(variant_element->value);
        }

        json_object_element_s* model_element = GetJsonObjectElement(state_details, POLY_STR("model"));
        if (model_element) {
          ArenaSnapshot snapshot = arena->GetSnapshot();

          json_string_s* model_name_json = json_value_as_string(model_element->value);
          String model_name(model_name_json->string, model_name_json->string_size);

          size_t prefix_size = poly_contains(model_name, ':') ? 10 : 0;
          model_name.data += prefix_size;
          model_name.size -= prefix_size;

          ParsedBlockModel** parsed_model = parsed_block_map.Find(MapStringKey(model_name.data, model_name.size));

          if (parsed_model && (*parsed_model)->parsed) {
            registry->states[bid].model = (*parsed_model)->model;
          } else {
            printf("Failed to find parsed_model %.*s\n", (u32)model_name.size, model_name.data);
          }

          element_set.Set(bid, 1);

          if (properties->size > 0) {
            String level_str = poly_strstr(*properties, "level=");

            if (level_str.data != nullptr) {
              char convert[16];

              memcpy(convert, level_str.data + 6, level_str.size - 6);
              convert[level_str.size - 6] = 0;

              int level = atoi(convert);

              assert(level >= 0 && level <= 15);

              registry->states[bid].leveled = true;
              registry->states[bid].level = level;
            }
          }

          arena->Revert(snapshot);
          variant_element = nullptr;

          json_object_element_s* state_element = state_details->start;

          Vector3i rotation;
          bool uvlock = false;

          // Loop through the other elements to see if there's any rotation in the variant
          while (state_element) {
            String element_name(state_element->name->string, state_element->name->string_size);

            if (poly_strcmp(element_name, POLY_STR("x")) == 0) {
              assert(state_element->value->type == json_type_number);
              int angle = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              rotation.x = angle;
            } else if (poly_strcmp(element_name, POLY_STR("y")) == 0) {
              assert(state_element->value->type == json_type_number);
              int angle = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              rotation.y = angle;
            } else if (poly_strcmp(element_name, POLY_STR("z")) == 0) {
              assert(state_element->value->type == json_type_number);
              int angle = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              rotation.z = angle;
            } else if (poly_strcmp(element_name, POLY_STR("uvlock")) == 0) {
              for (size_t i = 0; i < registry->states[bid].model.element_count; ++i) {
                uvlock = json_value_is_true(state_element->value);
              }
            }

            state_element = state_element->next;
          }

          if (parsed_model && (*parsed_model)->parsed) {
            RotateVariant(perm_arena, registry->states[bid].model, **parsed_model, 0,
                          registry->states[bid].model.element_count, rotation, uvlock);
          }
        }
      }

      if (variant_element) {
        variant_element = variant_element->next;
      }
    }
  }

  return true;
}

void AssetParser::ResolveModels(MemoryArena& perm_arena) {
  BitSet element_set(*this->arena, registry->state_count);

  for (size_t i = 0; i < model_count; ++i) {
    ParsedBlockModel* model = models + i;

    ResolveModel(*model);
  }

  for (size_t i = 0; i < state_count; ++i) {
    json_object_element_s* root_element = states[i].root->start;

    String blockstate_name = states[i].filename;
    blockstate_name.size -= 5;

    if (ResolveMultiparts(perm_arena, element_set, states + i, blockstate_name)) {
      // Resolved multiparts
    }

    if (ResolveVariants(perm_arena, element_set, states + i, blockstate_name)) {
      // Resolved variants
    }

    free(states[i].root_value);
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

} // namespace asset
} // namespace polymer
