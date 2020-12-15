#include "gamestate.h"

#include "json.h"
#include "math.h"
#include "zip_archive.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace polymer {

struct ChunkVertex {
  Vector3f position;
};

void PushVertex(MemoryArena* arena, ChunkVertex* vertices, u32* count, Vector3f position) {
  arena->Allocate(sizeof(ChunkVertex), 1);

  vertices[*count].position = position;

  ++*count;
}

void GameState::OnChunkLoad(s32 chunk_x, s32 chunk_y, s32 chunk_z) {
  // TODO:
  // Generate the chunk based on viewable blocks.
  // Skip air chunks.
  // The vertices will then be sent to the GPU and a handle will be stored to the allocation here.
  // When a chunk is unloaded, it will be freed from the GPU memory.
  // This should probably be done on a separate thread or in a compute shader ideally.

  // Either an index buffer or face merging should be done to reduce buffer size.

  ChunkSection* section = &chunks[GetChunkCacheIndex(chunk_x)][GetChunkCacheIndex(chunk_z)];

  u8* arena_snapshot = trans_arena->current;
  // Create an initial pointer to transient memory with zero vertices allocated.
  // Each push will allocate a new vertex with just a stack pointer increase so it's quick and contiguous.
  ChunkVertex* vertices = (ChunkVertex*)trans_arena->Allocate(0);
  u32 vertex_count = 0;

  for (u32 y = 0; y < 16; ++y) {
    for (u32 z = 0; z < 16; ++z) {
      for (u32 x = 0; x < 16; ++x) {
        u32 state_id = section->chunks[chunk_y].blocks[y][z][x];

        // TODO: Load the models/elements from the jar.
        if (state_id != 0) {
          // Render a single triangle on the top of the block
          PushVertex(trans_arena, vertices, &vertex_count, Vector3f((float)x, (float)y + 1, (float)z));
          PushVertex(trans_arena, vertices, &vertex_count, Vector3f((float)x + 1, (float)y + 1, (float)z + 1));
          PushVertex(trans_arena, vertices, &vertex_count, Vector3f((float)x + 1, (float)y + 1, (float)z));
        }
      }
    }
  }

  // RenderMesh mesh = renderer->AllocateMesh((u8*)vertices, sizeof(ChunkVertex) * vertex_count, vertex_count);
  // TODO: Free the mesh when chunk is unloaded, dimension is changed, or game is unloaded.
  // renderer->FreeMesh(&mesh);

  // Reset the arena to where it was before this allocation. The data was already sent to the GPU so it's no longer
  // useful.
  trans_arena->current = arena_snapshot;
}

void GameState::OnBlockChange(s32 x, s32 y, s32 z, u32 new_bid) {
  s32 chunk_x = (s32)std::floor(x / 16.0f);
  s32 chunk_z = (s32)std::floor(z / 16.0f);

  ChunkSection* section = &chunks[GetChunkCacheIndex(chunk_x)][GetChunkCacheIndex(chunk_z)];

  // It should be in the loaded cache otherwise the server is sending about an unloaded chunk.
  assert(section->x == chunk_x);
  assert(section->z == chunk_z);

  s32 relative_x = x % 16;
  s32 relative_z = z % 16;

  if (relative_x < 0) {
    relative_x += 16;
  }

  if (relative_z < 0) {
    relative_z += 16;
  }

  u32 old_bid = section->chunks[y / 16].blocks[y % 16][relative_z][relative_x];

  section->chunks[y / 16].blocks[y % 16][relative_z][relative_x] = (u32)new_bid;

#if 0
  printf("Block changed at (%d, %d, %d) from %s to %s\n", x, y, z, block_states[old_bid].name,
         block_states[new_bid].name);
#endif
}

bool GameState::LoadBlocks() {
  // TODO:
  // Read in all of the models in the jar
  // Read in blocks.json
  //  Serialize the properties into the same format as the jar's blockstates
  // Read in each blockstate from the jar
  //  Match the blockstate variant name to the blocks.json property value and store the id ?
  //  Read the model name for the block variant and match it to the model

  block_state_count = 0;

  FILE* f = fopen("blocks.json", "r");
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = memory_arena_push_type_count(trans_arena, char, file_size);

  fread(buffer, 1, file_size, f);
  fclose(f);

  json_value_s* root = json_parse(buffer, file_size);
  assert(root->type == json_type_object);

  json_object_s* root_obj = json_value_as_object(root);
  assert(root_obj);

  // Create a list of pointers to property strings stored in the transient arena
  char** properties = (char**)trans_arena->Allocate(sizeof(char*) * 32768);

  // Transient arena is used as a push buffer of property strings through parsing

  json_object_element_s* element = root_obj->start;
  while (element) {
    json_object_s* block_obj = json_value_as_object(element->value);
    assert(block_obj);

    char* block_name = block_names[block_name_count++];
    memcpy(block_name, element->name->string, element->name->string_size);

    json_object_element_s* block_element = block_obj->start;
    while (block_element) {
      if (strncmp(block_element->name->string, "states", block_element->name->string_size) == 0) {
        json_array_s* states = json_value_as_array(block_element->value);
        json_array_element_s* state_array_element = states->start;

        while (state_array_element) {
          json_object_s* state_obj = json_value_as_object(state_array_element->value);

          properties[block_state_count] = nullptr;

          json_object_element_s* state_element = state_obj->start;
          while (state_element) {
            if (strncmp(state_element->name->string, "id", state_element->name->string_size) == 0) {
              block_states[block_state_count].name = block_name;

              long block_id = strtol(json_value_as_number(state_element->value)->number, nullptr, 10);

              block_states[block_state_count].id = block_id;

              ++block_state_count;
            } else if (strncmp(state_element->name->string, "properties", state_element->name->string_size) == 0) {
              // Loop over each property and create a single string that matches the format of blockstates in the jar
              json_object_s* property_object = json_value_as_object(state_element->value);
              json_object_element_s* property_element = property_object->start;

              // Realign the arena for the property pointer to be 32-bit aligned.
              char* property = (char*)trans_arena->Allocate(0, 4);
              properties[block_state_count] = property;
              size_t property_length = 0;

              while (property_element) {
                json_string_s* property_value = json_value_as_string(property_element->value);
                // Allocate enough for property_name=property_value
                size_t alloc_size = property_element->name->string_size + 1 + property_value->string_size;

                property_length += alloc_size;

                char* p = (char*)trans_arena->Allocate(alloc_size, 1);

                // Allocate space for a comma to separate the properties
                if (property_element != property_object->start) {
                  trans_arena->Allocate(1, 1);
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

              trans_arena->Allocate(1, 1);
              properties[block_state_count][property_length] = 0;
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

  ZipArchive zip;
  if (!zip.Open("1.16.4.jar")) {
    fprintf(stderr, "Requires 1.16.4.jar.\n");
    return false;
  }

  size_t count;
  ZipArchiveElement* files = zip.ListFiles(trans_arena, "assets/minecraft/blockstates/", &count);

  // Loop through each blockstate asset and match the variant properties to the blocks.json list
  // TODO: Some of this could be sped up with hash maps, but not really necessary for now.
  // Alternatively, this data could all be loaded once, associated, and written off to a new asset file for faster loads
  for (size_t i = 0; i < count; ++i) {
    u8* arena_snapshot = trans_arena->current;
    size_t size;
    char* data = zip.ReadFile(trans_arena, files[i].name, &size);

    // Temporarily cut off the .json from the file name so the blockstate name is easily compared against
    files[i].name[strlen(files[i].name) - 5] = 0;
    // Amount of characters to skip over to get to the blockstate asset name
    constexpr size_t kBlockStateAssetSkip = 29;
    char* file_blockstate_name = files[i].name + kBlockStateAssetSkip;

    assert(data);

    json_value_s* root = json_parse(data, size);
    assert(root->type == json_type_object);

    json_object_s* root_obj = json_value_as_object(root);
    assert(root_obj);

    json_object_element_s* root_element = root_obj->start;

    while (root_element) {
      if (strncmp("variants", root_element->name->string, root_element->name->string_size) == 0) {
        json_object_s* variant_obj = json_value_as_object(root_element->value);

        json_object_element_s* variant_element = variant_obj->start;

        while (variant_element) {
          size_t block_id = -1;

          for (size_t bid = 0; bid < block_state_count; ++bid) {
            char* file_name = files[i].name;
            char* bid_name = block_states[bid].name + 10;

            if (strcmp(bid_name, file_blockstate_name) == 0) {
              block_id = bid;
              break;
            }
          }

          if (block_id != -1) {
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
              }
              state_element = state_element->next;
            }
          }

          variant_element = variant_element->next;
        }
      }
      root_element = root_element->next;
    }

    // Restore the .json to filename
    files[i].name[strlen(files[i].name) - 5] = '.';

    // Pop the current file off the stack allocator
    trans_arena->current = arena_snapshot;
  }

  zip.Close();

  return true;
}

} // namespace polymer
